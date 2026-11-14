/**
 * exec_expander.c - Word expansion implementation
 *
 * Performs POSIX word expansion in the context of execution frames.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arithmetic.h"
#include "exec_frame.h"
#include "miga/exec.h"
#include "exec_frame_expander.h"
#include "lexer.h"
#include "exec_types_internal.h"
#include "glob_util.h"
#include "logging.h"
#include "pattern_removal.h"
#include "miga/string_t.h"
#include "variable_store.h"
#include "miga/xalloc.h"

#ifdef MIGA_POSIX_API
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef MIGA_UCRT_API
#include <errno.h>
#include <io.h>
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Get the IFS value from the frame, falling back to default.
 */
static string_t *get_ifs(miga_frame_t *frame)
{
    Expects_not_null(frame);

    string_t *ifs_var = NULL;
    if (frame_has_variable_cstr(frame, "IFS"))
    {
        ifs_var = frame_get_variable_cstr(frame, "IFS");
        /* If IFS is empty, this had to be an explicit choice by the user, since
         * it is initialized to <space><tab><newline> by default. So we intentionally
         * don't check it here. */
    }
    else
    {
        char *ifs_env = getenv("IFS");
        if (ifs_env)
        {
            ifs_var = string_create_from_cstr(ifs_env);
        }
        else
        {
            /* Default IFS is space, tab, newline */
            ifs_var = string_create_from_cstr(" \t\n");
        }
    }

    return ifs_var;
}

/**
 * Record the exit status from a command substitution.
 */
static void record_subst_status(miga_frame_t *frame, int raw_status)
{
    if (!frame)
        return;

#ifdef MIGA_POSIX_API
    int status;
    if (WIFEXITED(raw_status))
    {
        status = WEXITSTATUS(raw_status);
    }
    else if (WIFSIGNALED(raw_status))
    {
        status = 128 + WTERMSIG(raw_status);
    }
    else
    {
        status = raw_status;
    }
#else
    int status = raw_status;
#endif

    frame->last_exit_status = status;
}

/**
 * Strip trailing newlines from a string (in-place modification via string API).
 */
static void strip_trailing_newlines(string_t *str)
{
    while (string_length(str) > 0)
    {
        char last = string_back(str);
        if (last == '\n' || last == '\r')
        {
            string_pop_back(str);
        }
        else
        {
            break;
        }
    }
}

/* ============================================================================
 * Tilde Expansion
 * ============================================================================ */

string_t *expand_tilde(miga_frame_t *frame, const string_t *username)
{
#ifdef MIGA_POSIX_API
    struct passwd *pw;

    if (username == NULL || string_length(username) == 0)
    {
        /* Expand ~ to current user's home */
        pw = getpwuid(getuid());
    }
    else
    {
        const char *uname = string_cstr(username);

        /* Check for special ~+ and ~- forms */
        if (strcmp(uname, "+") == 0)
        {
            /* ~+ expands to PWD */
            const char *pwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "PWD") : NULL;
            if (!pwd)
                pwd = getenv("PWD");
            if (pwd)
                return string_create_from_cstr(pwd);
            return NULL;
        }
        else if (strcmp(uname, "-") == 0)
        {
            /* ~- expands to OLDPWD */
            const char *oldpwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "OLDPWD") : NULL;
            if (!oldpwd)
                oldpwd = getenv("OLDPWD");
            if (oldpwd)
                return string_create_from_cstr(oldpwd);
            return NULL;
        }

        /* Expand ~username to specified user's home */
        pw = getpwnam(uname);
    }

    if (pw == NULL || pw->pw_dir == NULL)
    {
        return NULL;
    }

    return string_create_from_cstr(pw->pw_dir);

#elifdef MIGA_UCRT_API
    if (username == NULL || string_length(username) == 0)
    {
        /* Expand ~ to current user's home */
        const char *home = getenv("USERPROFILE");
        if (home == NULL)
        {
            home = getenv("HOME");
        }
        if (home != NULL)
        {
            return string_create_from_cstr(home);
        }
    }
    else
    {
        const char *uname = string_cstr(username);

        /* Check for special ~+ and ~- forms */
        if (strcmp(uname, "+") == 0)
        {
            const char *pwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "PWD") : NULL;
            if (!pwd)
                pwd = getenv("PWD");
            if (pwd)
                return string_create_from_cstr(pwd);
        }
        else if (strcmp(uname, "-") == 0)
        {
            const char *oldpwd = frame ? variable_store_get_value_cstr(
                exec_frame_get_variables(frame), "OLDPWD") : NULL;
            if (!oldpwd)
                oldpwd = getenv("OLDPWD");
            if (oldpwd)
                return string_create_from_cstr(oldpwd);
        }
    }
    /* ~username not supported on Windows */
    return NULL;

#else
    if (username == NULL || string_length(username) == 0)
    {
        /* ISO C: try HOME environment variable */
        const char *home = getenv("HOME");
        if (home != NULL)
        {
            return string_create_from_cstr(home);
        }
    }
    else
    {
        const char *uname = string_cstr(username);

        /* Check for special ~+ and ~- forms */
        if (strcmp(uname, "+") == 0)
        {
            const char *pwd = getenv("PWD");
            if (pwd)
                return string_create_from_cstr(pwd);
        }
        else if (strcmp(uname, "-") == 0)
        {
            const char *oldpwd = getenv("OLDPWD");
            if (oldpwd)
                return string_create_from_cstr(oldpwd);
        }
    }
    return NULL;
#endif
}

/* ============================================================================
 * Parameter Expansion
 * ============================================================================ */

/**
 * Get the value of a parameter (variable or special param).
 * Returns NULL if not set.
 */
static string_t *get_parameter_value(const miga_frame_t *frame, const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return NULL;
    }

    const char *name_cstr = string_cstr(name);

    /* Check for special parameters first */
    string_t *special = expand_special_param(frame, name);
    if (special)
    {
        return special;
    }

    /* Look up in variable store */
    if (frame)
    {
        /* Check local variables first (if frame supports them) */
        const string_t *value = exec_frame_get_variable(frame, name);
        if (value)
        {
            return string_create_from(value);
        }
    }

    /* Fall back to environment */
    const char *env_val = getenv(name_cstr);
    if (env_val)
    {
        return string_create_from_cstr(env_val);
    }

    return NULL; /* Not set */
}

/**
 * Set a variable in the frame.
 */
static void set_parameter_value(miga_frame_t *frame, const string_t *name, const string_t *value)
{
    if (!frame || !name || !value)
        return;

    variable_store_t *vars = exec_frame_get_variables(frame);
    if (vars)
    {
        variable_store_add_cstr(vars, string_cstr(name), string_cstr(value), false, false);
    }
}

/**
 * Check if a parameter is set (even if empty).
 */
static bool is_parameter_set(miga_frame_t *frame, const string_t *name)
{
    string_t *value = get_parameter_value(frame, name);
    if (value)
    {
        string_destroy(&value);
        return true;
    }
    return false;
}

/**
 * Expand parameter with modifiers (${var:-word}, ${var#pattern}, etc.)
 * Despite looking like an accessor, this function may have side effects
 * (e.g., modifying the frame's last_exit_status or assigning variables for := modifier).
 */
static string_t *expand_parts_to_string(miga_frame_t *frame, const part_list_t *parts);

/**
 * Expand the word of ${var-word}, ${var:=word}, ${var:+word}, ${var:?word}.
 * The lexer keeps it as raw text; it undergoes tilde, parameter, command and
 * arithmetic expansion (and quote removal) like any word, with no field
 * splitting. Inside a double-quoted expansion the word behaves as if it were
 * double-quoted itself.
 */
static string_t *expand_text_as_dquoted(miga_frame_t *frame, const string_t *body);

static string_t *expand_modifier_word(miga_frame_t *frame, const part_t *part)
{
    if (!part->word)
        return string_create();
    if (part_was_double_quoted(part))
        return expand_text_as_dquoted(frame, part->word);

    token_list_t *tokens = token_list_create();
    lex_status_t st = lex_cstr_to_tokens(string_cstr(part->word), tokens);
    string_t *result = NULL;
    if (st == LEX_OK && token_list_size(tokens) >= 1 &&
        token_get_type(token_list_get(tokens, 0)) == TOKEN_WORD)
    {
        result = expand_parts_to_string(frame, token_get_parts_const(token_list_get(tokens, 0)));
    }
    token_list_destroy(&tokens);
    if (!result)
        result = string_create_from(part->word);
    return result;
}

static string_t *expand_parameter_with_modifier(miga_frame_t *frame, const part_t *part)
{
    string_t *v = get_parameter_value(frame, part->param_name);
    bool is_set = (v != NULL);
    bool is_null = (v == NULL || string_length(v) == 0);
    /* ${var:-word} tests "unset or null"; ${var-word} tests "unset" only */
    bool missing = part->has_colon ? is_null : !is_set;

    switch (part->param_kind)
    {
    case PARAM_PLAIN:
        /* Simple parameter expansion */
        if (v)
        {
            return v;
        }
        return string_create();

    case PARAM_LENGTH:
        /* ${#var} - length of parameter */
        if (v)
        {
            int len = string_length(v);
            string_destroy(&v);
            return string_from_int(len);
        }
        return string_create_from_cstr("0");

    case PARAM_USE_DEFAULT:
        /* ${var:-word} / ${var-word} - substitute the expanded word */
        if (missing)
        {
            if (v)
                string_destroy(&v);
            return expand_modifier_word(frame, part);
        }
        return v;

    case PARAM_ASSIGN_DEFAULT:
        /* ${var:=word} / ${var=word} - assign the expanded word */
        if (missing)
        {
            if (v)
                string_destroy(&v);
            string_t *value = expand_modifier_word(frame, part);
            set_parameter_value(frame, part->param_name, value);
            return value;
        }
        return v;

    case PARAM_ERROR_IF_UNSET:
        /* ${var:?word} / ${var?word} - error with the expanded word */
        if (missing)
        {
            if (v)
                string_destroy(&v);
            string_t *msg = part->word ? expand_modifier_word(frame, part)
                                       : string_create_from_cstr("parameter null or not set");
            log_error("Parameter expansion error: %s: %s", string_cstr(part->param_name),
                      string_cstr(msg));
            string_destroy(&msg);
            if (frame)
            {
                frame->last_exit_status = 1;
            }
            return string_create();
        }
        return v;

    case PARAM_USE_ALTERNATE:
        /* ${var:+word} / ${var+word} - substitute the word when present */
        if (!missing)
        {
            if (v)
                string_destroy(&v);
            return expand_modifier_word(frame, part);
        }
        if (v)
            string_destroy(&v);
        return string_create();

    case PARAM_REMOVE_SMALL_SUFFIX:
        /* ${var%pattern} - remove smallest matching suffix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_suffix_smallest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_REMOVE_LARGE_SUFFIX:
        /* ${var%%pattern} - remove largest matching suffix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_suffix_largest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_REMOVE_SMALL_PREFIX:
        /* ${var#pattern} - remove smallest matching prefix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_prefix_smallest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_REMOVE_LARGE_PREFIX:
        /* ${var##pattern} - remove largest matching prefix */
        if (v)
        {
            if (part->word && string_length(part->word) > 0)
            {
                string_t *result = remove_prefix_largest(v, part->word);
                string_destroy(&v);
                return result;
            }
            return v;
        }
        return string_create();

    case PARAM_INDIRECT:
        /* ${!var} - indirect expansion */
        /* First, get the value of the named variable */
        if (v)
        {
            /* The value contains the name of another variable */
            /* Now expand that variable */
            string_t *indirect_value = get_parameter_value(frame, v);
            string_destroy(&v);

            if (indirect_value)
            {
                return indirect_value;
            }
            return string_create();
        }
        else
        {
            /* Variable not set, return empty */
            return string_create();
        }

    default:
        return string_create();
    }
}

string_t *expand_parameter(miga_frame_t *frame, const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return string_create();
    }

    string_t *value = get_parameter_value(frame, name);
    if (value)
    {
        return value;
    }

    return string_create(); /* Empty string if not set */
}

string_t *expand_special_param(const miga_frame_t *frame, const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return NULL;
    }

    const char *n = string_cstr(name);

    /* Single character special parameters */
    if (string_length(name) == 1)
    {
        switch (n[0])
        {
        case '?':
            /* Exit status of last command */
            if (frame)
            {
                return string_from_int(frame->last_exit_status);
            }
            return string_create_from_cstr("0");

        case '$':
            /* PID of shell */
            if (frame && frame->executor)
            {
                return string_from_int(frame->executor->shell_pid);
            }
            return string_create_from_cstr("0");

        case '!':
            /* PID of last background job */
            if (frame)
            {
                return string_from_int(frame->last_bg_pid);
            }
            return string_create_from_cstr("");

        case '#':
            /* Number of positional parameters */
            if (frame && frame->positional_params)
            {
                int count = positional_params_count(frame->positional_params);
                return string_from_int(count);
            }
            return string_create_from_cstr("0");

        case '@':
        case '*':
            /* All positional parameters joined with spaces (the unquoted
             * form, later field-split). Double-quoted "$@" and "$*" are
             * handled by expand_parts_to_fields(). */
            if (frame && frame->positional_params)
            {
                return positional_params_get_all_joined(frame->positional_params, ' ');
            }
            return string_create();

        case '-':
            /* Current option flags */
            /* TODO: Implement option flags string */
            return string_create();

        case '0':
            /* Shell or script name */
            if (frame && frame->positional_params)
            {
                const string_t *arg0 = positional_params_get_arg0(frame->positional_params);
                if (arg0)
                {
                    return string_create_from(arg0);
                }
            }
            return string_create_from_cstr("miga");
        }
    }

    /* Check for numeric positional parameters ($1, $2, ...) */
    char *endptr;
    long n_val = strtol(n, &endptr, 10);
    if (*endptr == '\0' && n_val >= 0)
    {
        if (frame && frame->positional_params)
        {
            if (n_val == 0)
            {
                const string_t *arg0 = positional_params_get_arg0(frame->positional_params);
                return arg0 ? string_create_from(arg0) : string_create();
            }
            else
            {
                const string_t *param = positional_params_get(frame->positional_params, (int)n_val);
                return param ? string_create_from(param) : string_create();
            }
        }
        return string_create();
    }

    return NULL; /* Not a special parameter */
}

bool is_special_param(const string_t *name)
{
    if (!name || string_length(name) == 0)
    {
        return false;
    }

    const char *n = string_cstr(name);

    /* Single character specials */
    if (string_length(name) == 1)
    {
        switch (n[0])
        {
        case '?':
        case '$':
        case '!':
        case '#':
        case '@':
        case '*':
        case '-':
        case '0':
            return true;
        }
    }

    /* Numeric positional parameters */
    char *endptr;
    strtol(n, &endptr, 10);
    return (*endptr == '\0');
}

/* ============================================================================
 * Command Substitution
 * ============================================================================ */

string_t *expand_command_subst(miga_frame_t *frame, const string_t *command)
{
#ifdef MIGA_POSIX_API
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        record_subst_status(frame, 0);
        return string_create();
    }

    /* POSIX 2.6.3: the command is executed in a subshell environment. Fork,
     * run it through this shell's own executor in the child with stdout
     * connected to a pipe, and collect the output in the parent. Running it
     * in-process (rather than popen(3), which uses /bin/sh) keeps shell
     * functions, aliases, unexported variables and builtins visible. */
    int fds[2];
    if (pipe(fds) == -1)
    {
        log_error("expand_command_subst: pipe failed: %s", strerror(errno));
        record_subst_status(frame, 1 << 8);
        return string_create();
    }

    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0)
    {
        log_error("expand_command_subst: fork failed: %s", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        record_subst_status(frame, 1 << 8);
        return string_create();
    }

    if (pid == 0)
    {
        /* Child: stdout goes to the pipe. */
        close(fds[0]);
        if (fds[1] != STDOUT_FILENO)
        {
            dup2(fds[1], STDOUT_FILENO);
            close(fds[1]);
        }

        miga_exec_t *executor = frame->executor;
        parse_session_t *session = exec_create_parse_session(executor);
        miga_exec_status_t st = MIGA_EXEC_STATUS_ERROR;
        if (session)
            st = exec_frame_string_core(frame, cmd, session);

        int code = executor->last_exit_status & 0xff;
        if (st == MIGA_EXEC_STATUS_INCOMPLETE)
            code = 2; /* syntax error: unterminated input */
        else if (st != MIGA_EXEC_STATUS_OK && st != MIGA_EXEC_STATUS_EXIT &&
                 st != MIGA_EXEC_STATUS_EMPTY && code == 0)
            code = 1;

        fflush(stdout);
        fflush(stderr);
        _exit(code);
    }

    /* Parent: read everything the child writes. */
    close(fds[1]);
    string_t *output = string_create();
    char buffer[4096];
    for (;;)
    {
        ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n > 0)
        {
            string_append_data(output, buffer, (int)n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
    close(fds[0]);

    int wstatus = 0;
    pid_t waited;
    do
    {
        waited = waitpid(pid, &wstatus, 0);
    } while (waited < 0 && errno == EINTR);
    record_subst_status(frame, waited < 0 ? (1 << 8) : wstatus);

    /* Strip trailing newlines per POSIX */
    strip_trailing_newlines(output);

    return output;

#elifdef MIGA_UCRT_API
    const char *cmd = string_cstr(command);
    if (cmd == NULL || *cmd == '\0')
    {
        record_subst_status(frame, 0);
        return string_create();
    }

    FILE *pipe = _popen(cmd, "r");
    if (pipe == NULL)
    {
        log_error("expand_command_subst: _popen failed for '%s'", cmd);
        record_subst_status(frame, 1);
        return string_create();
    }

    string_t *output = string_create();
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string_append_cstr(output, buffer);
    }

    int exit_code = _pclose(pipe);
    record_subst_status(frame, exit_code);

    strip_trailing_newlines(output);

    return output;

#else
    /* ISO C: no portable way to capture command output */
    (void)command;
    record_subst_status(frame, 0);
    return string_create();
#endif
}

/* ============================================================================
 * Arithmetic Expansion
 * ============================================================================ */

string_t *expand_arithmetic(miga_frame_t *frame, const string_t *expression)
{
    if (!expression)
    {
        return string_create_from_cstr("0");
    }

    if (!frame)
    {
        log_warn("expand_arithmetic: no frame available");
        return string_create_from_cstr("0");
    }

    /* Use the arithmetic module to evaluate the expression */
    ArithmeticResult result = arithmetic_evaluate(frame, expression);

    if (result.failed)
    {
        log_error("Arithmetic expansion error: %s",
                  result.error ? string_cstr(result.error) : "unknown error");
        arithmetic_result_free(&result);
        frame->last_exit_status = 1;
        return string_create_from_cstr("0");
    }

    string_t *value = string_from_int((int)result.value);
    arithmetic_result_free(&result);
    return value;
}

/* ============================================================================
 * Field Splitting
 * ============================================================================ */

/**
 * Check if a character is IFS whitespace (space, tab, newline).
 */
static bool is_ifs_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

/**
 * Check if a character is in the IFS string.
 */
static bool is_ifs_char(char c, const char *ifs)
{
    for (const char *p = ifs; *p; p++)
    {
        if (*p == c)
            return true;
    }
    return false;
}

/**
 * POSIX-compliant field splitting.
 *
 * Rules:
 * 1. If IFS is null (empty), no splitting occurs
 * 2. IFS whitespace (space/tab/newline) at start/end is ignored
 * 3. Consecutive IFS whitespace is treated as a single delimiter
 * 4. Non-whitespace IFS characters create empty fields
 * 5. IFS whitespace adjacent to non-whitespace IFS is ignored
 * 6. If the result contains only IFS whitespace, produce zero words (empty list)
 */
strlist_t *expand_field_split(miga_frame_t *frame, const string_t *text)
{
    Expects_not_null(frame);
    Expects_not_null(text);

    strlist_t *fields = strlist_create();

    if (string_empty(text))
        return fields;

    string_t *ifs = get_ifs(frame);
    if (string_empty(ifs))
    {
        // Empty IFS: no splitting, whole text is one field
        strlist_push_back(fields, text);
        string_destroy(&ifs);
        return fields;
    }

    // Split IFS into whitespace and non-whitespace parts for efficient searching
    string_t *ifs_ws = string_create();
    string_t *ifs_nws = string_create();
    for (int k = 0; k < string_length(ifs); k++)
    {
        char c = string_at(ifs, k);
        if (is_ifs_whitespace(c))
            string_append_char(ifs_ws, c);
        else
            string_append_char(ifs_nws, c);
    }

    int len = string_length(text);
    int i = 0;

    // 1. Skip all leading IFS whitespace (never produces fields)
    if (!string_empty(ifs_ws))
    {
        i = string_find_first_not_of_at(text, ifs_ws, 0);
        if (i == -1)
        {
            // Entire input is IFS whitespace → zero fields
            goto cleanup;
        }
    }
    // Now i is either at a non-IFS char or at a hard (non-ws) delimiter

    while (i < len)
    {
        // 2. Collect the field: all characters until any IFS char
        int field_start = i;
        int field_end = string_find_first_of_at(text, ifs, i);
        if (field_end == -1)
            field_end = len;

        string_t *field = string_create_from_range(text, field_start, field_end);
        strlist_move_push_back(fields, &field);

        i = field_end;
        if (i >= len)
            break;

        // 3. Consume the delimiter sequence
        //    - Skip all following IFS whitespace (collapsed)
        //    - Handle each non-whitespace IFS char as a separate delimiter (empty fields)
        bool in_delimiter = true;
        while (in_delimiter && i < len)
        {
            // Skip whitespace part of delimiter
            if (!string_empty(ifs_ws))
            {
                int after_ws = string_find_first_not_of_at(text, ifs_ws, i);
                if (after_ws != -1)
                    i = after_ws;
                else
                {
                    // Only trailing whitespace left → done, no trailing empty from ws-only
                    goto cleanup;
                }
            }

            // Now handle zero or more consecutive hard delimiters
            while (i < len && string_find_first_of_at(text, ifs_nws, i) == i)
            {
                // Consume this hard delimiter
                i++;

                // After each hard delimiter, we conceptually "end" a field.
                // If we're at end or followed by more delimiters → empty field
                if (i >= len || string_find_first_of_at(text, ifs, i) == i)
                {
                    string_t *empty = string_create();
                    strlist_move_push_back(fields, &empty);
                }
                else
                {
                    // Next is start of real field → stop consuming delimiters
                    in_delimiter = false;
                    break;
                }
            }

            // If we didn't hit another hard delimiter and whitespace is done,
            // we're at the start of the next field (or end)
            if (i < len && string_find_first_of_at(text, ifs, i) != i)
                in_delimiter = false;
        }
    }

cleanup:
    string_destroy(&ifs);
    string_destroy(&ifs_ws);
    string_destroy(&ifs_nws);
    return fields;
}

/* ============================================================================
 * Pathname Expansion
 * ============================================================================ */

strlist_t *expand_pathname(miga_frame_t *frame, const string_t *pattern)
{
    (void)frame; /* May use frame for noglob check in future */

    strlist_t *matches = glob_util_expand_path(pattern);

    if (matches && strlist_size(matches) > 0)
    {
        return matches;
    }

    /* No matches: return original pattern */
    if (matches)
    {
        strlist_destroy(&matches);
    }

    strlist_t *result = strlist_create();
    strlist_push_back(result, string_create_from(pattern));
    return result;
}

/* ============================================================================
 * Part Expansion (Internal)
 * ============================================================================ */

/**
 * Expand a single token part.
 * This function may have side effects
 * (e.g., modifying the frame's last_exit_status or assigning variables for := modifier).
 */
static string_t *expand_part(miga_frame_t *frame, const part_t *part)
{
    switch (part->type)
    {
    case PART_LITERAL:
        return string_create_from(part->text);

    case PART_PARAMETER:
        /* Use the full parameter expansion with modifiers */
        return expand_parameter_with_modifier(frame, part);

    case PART_COMMAND_SUBST: {
        /* The lexer stores the raw command text in part->text. (Older code
         * produced nested tokens instead; join them if that is all we have.) */
        if (part->text && string_length(part->text) > 0)
            return expand_command_subst(frame, part->text);

        string_t *cmd = string_create();
        int len = part->nested ? token_list_size(part->nested) : 0;
        for (int i = 0; i < len; i++)
        {
            const token_t *t = token_list_get(part->nested, i);
            string_t *ts = token_to_string(t);
            string_append(cmd, ts);
            string_destroy(&ts);
            if (i < token_list_size(part->nested) - 1)
            {
                string_append_cstr(cmd, " ");
            }
        }
        string_t *result = expand_command_subst(frame, cmd);
        string_destroy(&cmd);
        return result;
    }

    case PART_ARITHMETIC:
        return expand_arithmetic(frame, part->text);

    case PART_TILDE:
        return expand_tilde(frame, part->text);

    default:
        return string_create();
    }
}

/**
 * Expand all parts of a token to a single string.
 * Respects quoting: single-quoted parts are literal, double-quoted allow expansion.
 * May have side effects from parameter expansions with modifiers.
 */
static string_t *expand_parts_to_string(miga_frame_t *frame, const part_list_t *parts)
{
    Expects_not_null(frame);
    Expects_not_null(parts);

    string_t *result = string_create();

    for (int i = 0; i < part_list_size(parts); i++)
    {
        const part_t *part = part_list_get(parts, i);

        /* Single-quoted parts: no expansion, literal text */
        if (part->was_single_quoted && part->type == PART_LITERAL)
        {
            string_append(result, part->text);
            continue;
        }

        /* Expand the part */
        string_t *expanded = expand_part(frame, part);

        if (expanded)
        {
            string_append(result, expanded);
            string_destroy(&expanded);
        }
    }

    return result;
}

/**
 * Expand the parts of a word into fields. Every part contributes to the
 * current field, except a double-quoted "$@", which contributes its first
 * positional parameter to the current field, each middle one as a field of
 * its own, and starts the next field with the last one (POSIX 2.5.2). A word
 * made only of "$@" with no positional parameters yields no field.
 */
static strlist_t *expand_parts_to_fields(miga_frame_t *frame, const part_list_t *parts,
                                         bool *has_quoted_at)
{
    strlist_t *fields = strlist_create();
    string_t *current = string_create();
    bool emitted_anything = false;

    for (int i = 0; i < part_list_size(parts); i++)
    {
        const part_t *part = part_list_get(parts, i);

        bool quoted_special = part->type == PART_PARAMETER && part->param_kind == PARAM_PLAIN &&
                              part_was_double_quoted(part) && part->param_name &&
                              string_length(part->param_name) == 1;
        bool quoted_at = quoted_special && string_cstr(part->param_name)[0] == '@';
        bool quoted_star = quoted_special && string_cstr(part->param_name)[0] == '*';
        if (quoted_star)
        {
            /* "$*": one field, parameters joined with the first character of
             * IFS (a space when IFS is unset, nothing when it is empty). */
            emitted_anything = true;
            strlist_t *all = frame && frame->positional_params
                                 ? positional_params_get_all(frame->positional_params)
                                 : strlist_create();
            string_t *sep = string_create_from_cstr(" ");
            if (frame && frame_has_variable_cstr(frame, "IFS"))
            {
                string_t *ifs = frame_get_variable_cstr(frame, "IFS");
                string_clear(sep);
                if (string_length(ifs) > 0)
                    string_append_char(sep, string_cstr(ifs)[0]);
                string_destroy(&ifs);
            }
            for (int j = 0; j < strlist_size(all); j++)
            {
                if (j > 0)
                    string_append(current, sep);
                string_append(current, strlist_at(all, j));
            }
            string_destroy(&sep);
            strlist_destroy(&all);
            continue;
        }
        if (quoted_at)
        {
            if (has_quoted_at)
                *has_quoted_at = true;
            strlist_t *all = frame && frame->positional_params
                                 ? positional_params_get_all(frame->positional_params)
                                 : strlist_create();
            int n = strlist_size(all);
            for (int j = 0; j < n; j++)
            {
                string_append(current, strlist_at(all, j));
                if (j < n - 1)
                {
                    strlist_move_push_back(fields, &current);
                    current = string_create();
                }
            }
            if (n > 0)
                emitted_anything = true;
            strlist_destroy(&all);
            continue;
        }

        emitted_anything = true;
        if (part->was_single_quoted && part->type == PART_LITERAL)
        {
            string_append(current, part->text);
            continue;
        }
        string_t *expanded = expand_part(frame, part);
        if (expanded)
        {
            string_append(current, expanded);
            string_destroy(&expanded);
        }
    }

    if (emitted_anything)
        strlist_move_push_back(fields, &current);
    else
        string_destroy(&current);
    return fields;
}

/**
 * Remove quote characters from a string (final step of word expansion).
 * Note: This is typically handled during tokenization/parsing, but included
 * for completeness in the expansion sequence.
 */
static string_t *remove_quotes(const string_t *text)
{
    /* In our implementation, quotes are already removed during tokenization.
     * Parts track was_single_quoted and was_double_quoted flags.
     * This function is a no-op but kept for API completeness. */
    return string_create_from(text);
}

/* ============================================================================
 * High-Level Expansion Functions
 * ============================================================================ */

/**
 * Expands a single WORD token into a list of strings, applying all relevant expansions.
 * This may have side effects from parameter expansions with modifiers and command substitutions.
 */
strlist_t *exec_frame_expander_expand_word(miga_frame_t *frame, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }

    /* Check if any expansion is needed */
    if (!tok->needs_expansion && !tok->needs_field_splitting && !tok->needs_pathname_expansion)
    {
        /* No expansion: return literal text */
        string_t *text = token_get_all_text(tok);
        strlist_t *result = strlist_create();
        strlist_move_push_back(result, &text);
        return result;
    }

    /* Expand all parts. A double-quoted "$@" yields one field per positional
     * parameter (and no field at all when there are none). */
    bool has_quoted_at = false;
    strlist_t *fields = expand_parts_to_fields(frame, token_get_parts_const(tok), &has_quoted_at);

    /* Field splitting of the unquoted expansions. (When the word also holds a
     * quoted "$@", its fields are kept whole.) */
    if (tok->needs_field_splitting && !has_quoted_at)
    {
        strlist_t *split = strlist_create();
        for (int i = 0; i < strlist_size(fields); i++)
        {
            strlist_t *parts = expand_field_split(frame, strlist_at(fields, i));
            for (int j = 0; j < strlist_size(parts); j++)
                strlist_push_back(split, string_create_from(strlist_at(parts, j)));
            strlist_destroy(&parts);
        }
        strlist_destroy(&fields);
        fields = split;

        /* POSIX: If field splitting produced zero words (e.g., input was only
         * IFS whitespace), that's correct - don't add an empty string. */
    }

    /* Pathname expansion */
    if (tok->needs_pathname_expansion)
    {
        strlist_t *globs = strlist_create();

        for (int i = 0; i < strlist_size(fields); i++)
        {
            const string_t *pattern = strlist_at(fields, i);
            strlist_t *matches = expand_pathname(frame, pattern);

            for (int j = 0; j < strlist_size(matches); j++)
            {
                strlist_push_back(globs, string_create_from(strlist_at(matches, j)));
            }
            strlist_destroy(&matches);
        }

        strlist_destroy(&fields);
        return globs;
    }

    return fields;
}

string_t *expand_word_nosplit(miga_frame_t *frame, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }

    /* If no expansion is needed at all, return the literal text */
    if (!tok->needs_expansion)
    {
        return token_get_all_text(tok);
    }

    /* Expand parts (tilde, parameter, command subst, arithmetic)
     * but skip field splitting and pathname expansion */
    return expand_parts_to_string(frame, token_get_parts_const(tok));
}

/**
 * Expand a list of WORD tokens into a list of strings, applying all relevant expansions.
 * This may have side effects from parameter expansions with modifiers and command substitutions.
 */
strlist_t *expand_words(miga_frame_t *frame, const token_list_t *tokens)
{
    if (!tokens)
    {
        return NULL;
    }

    strlist_t *result = strlist_create();

    for (int i = 0; i < token_list_size(tokens); i++)
    {
        const token_t *tok = token_list_get(tokens, i);
        strlist_t *expanded = exec_frame_expander_expand_word(frame, tok);

        if (expanded)
        {
            for (int j = 0; j < strlist_size(expanded); j++)
            {
                strlist_push_back(result, string_create_from(strlist_at(expanded, j)));
            }
            strlist_destroy(&expanded);
        }
    }

    return result;
}

string_t *expand_string(miga_frame_t *frame, const string_t *text, expand_flags_t flags)
{
    /* TODO: Implement proper string expansion with flags */
    /* For now, just return a copy if no expansion requested */
    (void)frame;
    (void)flags;
    return string_create_from(text);
}

string_t *expand_redirection_target(miga_frame_t *frame, const token_t *tok)
{
    if (!tok || tok->type != TOKEN_WORD)
    {
        return NULL;
    }

    /* Redirection targets: tilde, parameter, command, arithmetic expansion
     * but NO field splitting or pathname expansion */
    return expand_parts_to_string(frame, token_get_parts_const(tok));
}

string_t *expand_assignment_value(miga_frame_t *frame, const token_t *tok)
{
    Expects_not_null(frame);
    Expects_not_null(tok);
    Expects_eq(tok->type, TOKEN_ASSIGNMENT_WORD);

    /* Assignment values: tilde, parameter, command, arithmetic expansion
     * but NO field splitting or pathname expansion */
    return expand_parts_to_string(frame, tok->assignment_value);
}

string_t *expand_heredoc(miga_frame_t *frame, const string_t *body, bool is_quoted)
{
    if (is_quoted)
    {
        /* Quoted delimiter: the body is literal */
        return string_create_from(body);
    }
    return expand_text_as_dquoted(frame, body);
}

/**
 * Expand raw text as the contents of a double-quoted string (used for
 * unquoted here-document bodies and for ${var-word} words inside double
 * quotes).
 */
static string_t *expand_text_as_dquoted(miga_frame_t *frame, const string_t *body)
{

    /* POSIX 2.7.4: with an unquoted delimiter the body undergoes parameter
     * expansion, command substitution and arithmetic expansion, and backslash
     * quotes only $ ` \ and newline: exactly the rules for the contents of a
     * double-quoted string, except that the double-quote character itself is
     * ordinary. So wrap the body in double quotes, escaping every literal
     * double quote (a backslash before one must be kept as a backslash), lex
     * it as one word, and expand the word's parts with no field splitting.
     * Quotes inside $(...), $((...)) and `...` are part of that command or
     * expression and are left alone. */
    const char *text = string_cstr(body);
    int len = string_length(body);
    string_t *quoted = string_create();
    string_append_char(quoted, '"');
    int paren_depth = 0;   /* inside $( ... ) or $(( ... )) */
    bool in_backquote = false;
    for (int i = 0; i < len; i++)
    {
        char c = text[i];
        if (c == '\\' && i + 1 < len)
        {
            /* Copy the pair unchanged, except that \" must stay a literal
             * backslash followed by a literal quote. */
            char next = text[i + 1];
            if (next == '"' && paren_depth == 0 && !in_backquote)
                string_append_cstr(quoted, "\\\\\\\"");
            else
            {
                string_append_char(quoted, c);
                string_append_char(quoted, next);
            }
            i++;
            continue;
        }
        if (!in_backquote && c == '$' && i + 1 < len && text[i + 1] == '(')
        {
            paren_depth++;
            string_append_char(quoted, c);
            string_append_char(quoted, '(');
            i++;
            continue;
        }
        if (paren_depth > 0)
        {
            if (c == '(')
                paren_depth++;
            else if (c == ')')
                paren_depth--;
            string_append_char(quoted, c);
            continue;
        }
        if (c == '`')
        {
            in_backquote = !in_backquote;
            string_append_char(quoted, c);
            continue;
        }
        if (c == '"' && !in_backquote)
        {
            string_append_cstr(quoted, "\\\"");
            continue;
        }
        string_append_char(quoted, c);
    }
    string_append_char(quoted, '"');

    token_list_t *tokens = token_list_create();
    lex_status_t st = lex_cstr_to_tokens(string_cstr(quoted), tokens);
    string_destroy(&quoted);

    string_t *result = NULL;
    if (st == LEX_OK && token_list_size(tokens) >= 1 &&
        token_get_type(token_list_get(tokens, 0)) == TOKEN_WORD)
    {
        result = expand_parts_to_string(frame, token_get_parts_const(token_list_get(tokens, 0)));
    }
    token_list_destroy(&tokens);

    if (!result)
    {
        log_warn("expand_heredoc: could not lex here-document body; using it unexpanded");
        result = string_create_from(body);
    }
    return result;
}

/* ============================================================================
 * Legacy/Compatibility Functions
 * ============================================================================ */

strlist_t *exec_expand_word(miga_exec_t *executor, const token_t *tok)
{
    if (!executor)
        return NULL;
    return exec_frame_expander_expand_word(executor->current_frame, tok);
}

strlist_t *exec_expand_words(miga_exec_t *executor, const token_list_t *tokens)
{
    if (!executor)
        return NULL;
    return expand_words(executor->current_frame, tokens);
}

string_t *exec_expand_redirection_target(miga_exec_t *executor, const token_t *tok)
{
    if (!executor)
        return NULL;
    return expand_redirection_target(executor->current_frame, tok);
}

string_t *exec_expand_assignment_value(miga_exec_t *executor, const token_t *tok)
{
    if (!executor)
        return NULL;
    return expand_assignment_value(executor->current_frame, tok);
}

string_t *exec_expand_heredoc(miga_exec_t *executor, const string_t *body, bool is_quoted)
{
    if (!executor)
        return NULL;
    return expand_heredoc(executor->current_frame, body, is_quoted);
}

string_t *exec_expand_tilde(miga_exec_t *executor, const string_t *text)
{
    miga_frame_t *frame = executor ? executor->current_frame : NULL;
    return expand_tilde(frame, text);
}

/* ============================================================================
 * Callback for External Use
 * ============================================================================ */

string_t *exec_command_subst_callback(void *userdata, const string_t *command)
{
    miga_exec_t *executor = (miga_exec_t *)userdata;
    miga_frame_t *frame = executor ? executor->current_frame : NULL;
    return expand_command_subst(frame, command);
}
