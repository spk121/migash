#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>



#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif

#include "exec_redirect.h"

#include "ast.h"
#include "exec_frame.h"
#include "exec_frame_expander.h"
#include "exec_types_internal.h"
#include "fd_table.h"
#include "lib.h"
#include "logging.h"
#include "miga/string_t.h"
#include "token.h"
#include "miga/xalloc.h"

/**
 * Result of parsing a file descriptor number.
 */
typedef struct
{
    bool success;
    int fd;               /**< -1 means "just close" case (plain "-") */
    bool close_after_use; /**< true if we saw trailing '-' after a number */
} parse_fd_result_t;

/**
 * Parse a file descriptor number from a string.
 *
 * @param str     The string to parse (e.g., "1", "2")
 * @param out_fd  Output parameter for the parsed fd
 * @return        true on success, false if str is not a valid non-negative integer
 *
 * Unlike atoi(), this function:
 * - Rejects empty strings
 * - Rejects strings with non-digit characters (except leading whitespace)
 * - Rejects negative numbers
 * - Detects overflow
 */

static parse_fd_result_t parse_fd_number(const char *str)
{
    parse_fd_result_t result = {.success = false, .fd = -1, .close_after_use = false};

    if (str == NULL || *str == '\0')
        return result;

    /* Reject leading whitespace (POSIX: io_number must be adjacent to operator) */
    if (*str == ' ' || *str == '\t')
        return result;

    /* Special case: plain "-" → close the target fd */
    if (*str == '-' && (str[1] == '\0' || (str[1] == ' ' || str[1] == '\t')))
    {
        // Consume the '-' and any trailing whitespace
        str++;
        while (*str == ' ' || *str == '\t')
            str++;
        if (*str == '\0')
        {
            result.success = true;
            result.fd = -1; // marker for "just close"
            return result;
        }
        // else: trailing junk after plain '-', fall through to failure
    }

    /* Reject standalone negative sign or negative numbers */
    if (*str == '-')
        return result;

    /* Allow optional leading '+' */
    if (*str == '+')
    {
        log_warn("parse_fd_number: FD string has leading '+': '%s'", str);
        str++;
    }

    if (*str == '\0' || !(*str >= '0' && *str <= '9'))
        return result;

    /* Parse digits with overflow check */
    long long val = 0;
    const char *start_digits = str;
    while (*str >= '0' && *str <= '9')
    {
        val = val * 10 + (long long)(*str - '0');
        if (val > INT_MAX) // You could also check INT_MIN if you want to be pedantic
            return result;
        str++;
    }

    /* Now we expect either end-of-string or a trailing '-' (move+close) */
    bool saw_minus = false;
    if (*str == '-')
    {
        saw_minus = true;
        str++;
    }

    /* Skip trailing whitespace */
    while (*str == ' ' || *str == '\t')
        str++;

    /* Must be end of string now — no trailing garbage allowed */
    if (*str != '\0')
        return result;

    /* If we saw '-', it must have come right after digits (no space) */
    if (saw_minus && str == start_digits)
    {
        // "-" was not after digits — invalid
        return result;
    }

    result.success = true;
    result.fd = (int)val;
    result.close_after_use = saw_minus;
    return result;
}

/**
 * Return the default target FD for a redirection, following shell rules.
 * POSIX: <, <<, <> default to 0 (stdin); >, >>, >| default to 1 (stdout).
 */
static int redirection_default_fd(const exec_redirection_t *r)
{
    if (r->explicit_fd >= 0)
        return r->explicit_fd;
    switch (r->type)
    {
    case REDIR_READ:
    case REDIR_FROM_BUFFER:
    case REDIR_FROM_BUFFER_STRIP:
    case REDIR_READWRITE:
    case REDIR_FD_DUP_IN: /* <&n and <&- act on standard input */
        return 0;
    default:
        return 1;
    }
}

/* ============================================================================
 * Save sets
 * ============================================================================
 * (Used by the POSIX implementation; the helpers are plain ISO C.)
 * Every application of a redirection list records what it saved in a set
 * that is pushed on executor->redir_saves. The matching restore (or commit,
 * for "exec") pops that set and undoes (or forgets) exactly those saves, so
 * nested redirections and redirections around compound commands whose bodies
 * run further commands in the same process are undone in LIFO order.
 */
typedef struct
{
    int fd;     /* the redirected descriptor */
    int backup; /* copy of its previous open file, or -1 when it was closed */
    /* Stream slots (configurations without descriptors): the frame's
     * stdin/stdout/stderr slot that was changed, its previous value, and
     * whether the stream now in the slot was opened by this redirection. */
    FILE **slot;
    FILE *previous;
    bool owned;
} redir_saved_fd_t;

struct redir_save_set_t
{
    redir_saved_fd_t *items;
    size_t count;
    size_t capacity;
    struct redir_save_set_t *next;
};

static struct redir_save_set_t *save_set_push(miga_exec_t *executor)
{
    struct redir_save_set_t *set = xcalloc(1, sizeof(*set));
    set->next = executor->redir_saves;
    executor->redir_saves = set;
    return set;
}

static struct redir_save_set_t *save_set_pop(miga_exec_t *executor)
{
    struct redir_save_set_t *set = executor->redir_saves;
    if (set)
        executor->redir_saves = set->next;
    return set;
}

static void save_set_destroy(struct redir_save_set_t *set)
{
    if (!set)
        return;
    xfree(set->items);
    xfree(set);
}

static redir_saved_fd_t *save_set_new_item(struct redir_save_set_t *set)
{
    if (set->count == set->capacity)
    {
        set->capacity = set->capacity ? set->capacity * 2 : 4;
        set->items = xrealloc(set->items, set->capacity * sizeof(*set->items));
    }
    redir_saved_fd_t *item = &set->items[set->count++];
    item->fd = -1;
    item->backup = -1;
    item->slot = NULL;
    item->previous = NULL;
    item->owned = false;
    return item;
}

static void save_set_add(struct redir_save_set_t *set, int fd, int backup)
{
    redir_saved_fd_t *item = save_set_new_item(set);
    item->fd = fd;
    item->backup = backup;
}

static void save_set_add_slot(struct redir_save_set_t *set, FILE **slot, bool owned)
{
    redir_saved_fd_t *item = save_set_new_item(set);
    item->slot = slot;
    item->previous = *slot;
    item->owned = owned;
}


// If we're calling an internal function like a builtin or a function defined in the current script,
// in ISO C mode we can support redirections by by setting the *_stdin, *_stdout, and *_stderr
// fields which can be passed to the exec_builtin_context_t struct which the internal function may
// use as the basis for its I/O. However, this only works for the three standard streams, and it is
// up to the builtin function implementation to actually use these fields correctly.

// For external commands, which are called using system(), no redirection mechanism is possible.
// However, there is the MIGA_ENV_VAR environment variable which will cause a temp file to be
// created with environment variable content. This is implemented elsewhere.
miga_exec_status_t exec_apply_redirections_iso_c(miga_frame_t *frame, const exec_redirections_t *redirs)
{
    Expects_not_null(frame);
    Expects_not_null(redirs);

    miga_exec_t *executor = frame->executor;
    /* Pushed even when empty: every apply is matched by one restore/commit.
     * Each change of a stream slot is recorded so that the matching restore
     * undoes exactly this application (an enclosing redirection stays). */
    struct redir_save_set_t *saves = save_set_push(executor);
    if (redirs->count == 0)
        return MIGA_EXEC_STATUS_OK;

    for (size_t i = 0; i < redirs->count; i++)
    {
        const exec_redirection_t *r = &redirs->items[i];

        int target_fd = redirection_default_fd(r);
        if (target_fd < 0)
        {
            exec_set_error_printf(executor, "Invalid target FD");
            goto error_restore;
        }

        /* ISO C only supports the three standard streams (builtins use the context FILE* fields) */
        if (target_fd > 2)
        {
            log_warn("ISO C redirection: fd %d not supported (only 0/1/2 work for builtins)",
                     target_fd);
            continue;
        }

        FILE **target_ptr = NULL;
        switch (target_fd)
        {
        case 0:
            target_ptr = frame->stdin_fp;
            break;
        case 1:
            target_ptr = frame->stdout_fp;
            break;
        case 2:
            target_ptr = frame->stderr_fp;
            break;
        default:
            continue;
        }

        switch (r->target_kind)
        {
        case REDIR_TARGET_FILE: {
            string_t *fname_str = NULL;
            if (!r->target.file.is_expanded)
                fname_str = expand_redirection_target(frame, r->target.file.tok);
            else
                fname_str = string_create_from(r->target.file.filename);

            if (!fname_str)
            {
                exec_set_error_printf(executor, "Failed to expand file target");
                goto error_restore;
            }

            const char *fname = string_cstr(fname_str);
            const char *mode = "r";

            switch (r->type)
            {
            case REDIR_READ:
                mode = "r";
                break;
            case REDIR_WRITE:
                /* noclobber support (C11 "wx" is nice but we stay C89-compatible) */
                mode = (frame->opt_flags && frame->opt_flags->noclobber) ? "w" : "w";
                break;
            case REDIR_APPEND:
                mode = "a";
                break;
            case REDIR_READWRITE:
                mode = "r+"; /* best portable approximation (creates only if file exists) */
                break;
            case REDIR_WRITE_FORCE:
                mode = "w";
                break;
            default:
                exec_set_error_printf(executor, "Unsupported redirection type %d", r->type);
                string_destroy(&fname_str);
                goto error_restore;
            }

            FILE *new_stream = fopen(fname, mode);
            if (!new_stream)
            {
                exec_set_error_printf(executor, "fopen('%s') failed: %s", fname, strerror(errno));
                string_destroy(&fname_str);
                goto error_restore;
            }

            save_set_add_slot(saves, target_ptr, true);
            *target_ptr = new_stream;
            string_destroy(&fname_str);
            break;
        }

        case REDIR_TARGET_BUFFER: {
            string_t *content_str = NULL;
            if (r->target.heredoc.content)
            {
                content_str = r->target.heredoc.needs_expansion
                                  ? expand_heredoc(frame, r->target.heredoc.content, false)
                                  : string_create_from(r->target.heredoc.content);
            }

            FILE *tmp = tmpfile();
            if (!tmp)
            {
                exec_set_error_printf(executor, "tmpfile() failed for heredoc");
                string_destroy(&content_str);
                goto error_restore;
            }

            const char *content = content_str ? string_cstr(content_str) : "";
            size_t len = content_str ? string_length(content_str) : 0;
            if (len > 0 && fwrite(content, 1, len, tmp) != len)
            {
                exec_set_error_printf(executor, "write to heredoc tmpfile failed");
                fclose(tmp);
                string_destroy(&content_str);
                goto error_restore;
            }
            rewind(tmp);

            save_set_add_slot(saves, target_ptr, true);
            *target_ptr = tmp;
            string_destroy(&content_str);
            break;
        }

        case REDIR_TARGET_FD: {
            /* Limited support: only 0/1/2 → share the FILE* pointer (no real dup/close) */
            string_t *fd_str = NULL;
            bool fd_allocated = false;
            if (r->target.fd.fd_expression)
            {
                fd_str = string_create_from(r->target.fd.fd_expression);
                fd_allocated = true;
            }
            else if (r->target.fd.fixed_fd >= 0)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", r->target.fd.fixed_fd);
                fd_str = string_create_from_cstr(buf);
                fd_allocated = true;
            }
            else if (r->target.fd.fd_token)
            {
                fd_str = expand_redirection_target(frame, r->target.fd.fd_token);
                fd_allocated = true;
            }

            if (!fd_str)
            {
                exec_set_error_printf(executor, "Failed to expand FD target");
                goto error_restore;
            }

            const char *lex = string_cstr(fd_str);
            parse_fd_result_t src = parse_fd_number(lex);
            if (fd_allocated)
                string_destroy(&fd_str);

            if (!src.success)
            {
                exec_set_error_printf(executor, "Invalid source FD: '%s'", lex);
                goto error_restore;
            }

            if (src.fd == -1)
            {
                /* n<&- or n>&- → the slot becomes empty (the builtin's stream
                 * accessor then returns the real standard stream) */
                save_set_add_slot(saves, target_ptr, false);
                *target_ptr = NULL;
                break;
            }

            /* Only support sharing among the three standard streams */
            FILE *src_stream = NULL;
            if (src.fd == 0)
                src_stream = *frame->stdin_fp;
            else if (src.fd == 1)
                src_stream = *frame->stdout_fp;
            else if (src.fd == 2)
                src_stream = *frame->stderr_fp;

            if (src_stream)
            {
                save_set_add_slot(saves, target_ptr, false);
                *target_ptr = src_stream; /* share (close_after_use ignored - no real fd) */
            }
            else
            {
                log_warn("ISO C: fd-to-fd source %d not a standard stream, ignoring", src.fd);
            }
            break;
        }

        case REDIR_TARGET_CLOSE: {
            save_set_add_slot(saves, target_ptr, false);
            *target_ptr = NULL;
            break;
        }

        default:
            exec_set_error_printf(executor, "Unsupported redirection kind %d in ISO C mode",
                           r->target_kind);
            goto error_restore;
        }
    }

    return MIGA_EXEC_STATUS_OK;

error_restore:
    /* Partial redirections are rolled back by the existing restore function
     * (it simply closes any non-NULL FILE* we allocated and sets the pointers
     *  to NULL, which is exactly the contract described in the stub comment). */
    exec_restore_redirections_iso_c(frame);
    return MIGA_EXEC_STATUS_ERROR;
}

void exec_restore_redirections_iso_c(miga_frame_t *frame)
{
    if (!frame || !frame->executor)
        return;
    struct redir_save_set_t *saves = save_set_pop(frame->executor);
    if (!saves)
        return;

    /* Undo in reverse order: close the streams this application opened and
     * put the previous stream (an enclosing redirection's, or NULL) back. */
    for (size_t k = saves->count; k-- > 0;)
    {
        redir_saved_fd_t *item = &saves->items[k];
        if (!item->slot)
            continue;
        FILE *current = *item->slot;
        if (item->owned && current && current != item->previous)
            fclose(current);
        else if (current)
            fflush(current);
        *item->slot = item->previous;
    }
    save_set_destroy(saves);
}

void exec_redirect_restore_redirections(miga_frame_t *frame, const exec_redirections_t *redirections)
{
    (void)redirections;
    exec_restore_redirections_iso_c(frame);
}

void exec_redirect_commit_redirections(miga_frame_t *frame)
{
    if (!frame)
        return;
    fd_table_t *fds = exec_frame_get_fds(frame);
    struct redir_save_set_t *saves = save_set_pop(frame->executor);
    if (!saves)
        return;

    for (size_t k = 0; k < saves->count; k++)
    {
        int fd = saves->items[k].fd;
        int backup_fd = saves->items[k].backup;
        if (backup_fd < 0)
        {
            if (fds)
                fd_table_clear_flag(fds, fd, FD_IS_RESTORE_CLOSE);
            continue;
        }
        if (fds)
            fd_table_remove(fds, backup_fd);
    }
    save_set_destroy(saves);
}

/* ============================================================================
 * Redirection Structure Management
 * ============================================================================ */

/**
 * Create a new empty redirection structure.
 */
exec_redirections_t *exec_redirections_create(void)
{
    exec_redirections_t *redirs = xcalloc(1, sizeof(exec_redirections_t));
    if (!redirs)
        return NULL;

    redirs->items = NULL;
    redirs->count = 0;
    redirs->capacity = 0;
    return redirs;
}

/**
 * Destroy a redirection structure and free all associated memory.
 */
void exec_redirections_destroy(exec_redirections_t **redirections)
{
    if (!redirections || !*redirections)
        return;

    exec_redirections_t *redirs = *redirections;

    if (redirs->items)
    {
        for (size_t i = 0; i < redirs->count; i++)
        {
            exec_redirection_t *redir = &redirs->items[i];

            /* Free target data based on type */
            switch (redir->target_kind)
            {
            case REDIR_TARGET_FILE:
                if (redir->target.file.filename)
                    string_destroy(&redir->target.file.filename);
                if (redir->target.file.tok)
                    token_destroy(&redir->target.file.tok);
                break;
            case REDIR_TARGET_FD:
                if (redir->target.fd.fd_expression)
                    string_destroy(&redir->target.fd.fd_expression);
                if (redir->target.fd.fd_token)
                    token_destroy(&redir->target.fd.fd_token);
                break;
            case REDIR_TARGET_BUFFER:
                if (redir->target.heredoc.content)
                    string_destroy(&redir->target.heredoc.content);
                break;
            case REDIR_TARGET_CLOSE:
            case REDIR_TARGET_FD_STRING:
            case REDIR_TARGET_INVALID:
            default:
                break;
            }

            /* Free location variable name if present */
            if (redir->io_location_varname)
                string_destroy(&redir->io_location_varname);
        }
        xfree(redirs->items);
    }

    xfree(*redirections);
    *redirections = NULL;
}

/**
 * Append a redirection to the structure.
 */
bool exec_redirections_append(exec_redirections_t *redirections, exec_redirection_t *redir)
{
    if (!redirections || !redir)
        return false;

    /* Grow capacity if needed */
    if (redirections->count >= redirections->capacity)
    {
        size_t new_capacity = redirections->capacity == 0 ? 4 : redirections->capacity * 2;
        exec_redirection_t *new_items =
            xrealloc(redirections->items, new_capacity * sizeof(exec_redirection_t));

        redirections->items = new_items;
        redirections->capacity = new_capacity;
    }

    /* Copy the redirection */
    redirections->items[redirections->count] = *redir;
    redirections->count++;

    return true;
}

exec_redirections_t *exec_redirections_clone(const exec_redirections_t *redirs)
{
    if (!redirs)
        return NULL;

    exec_redirections_t *clone = exec_redirections_create();
    if (!clone)
        return NULL;

    /* Pre-allocate capacity to match source */
    if (redirs->capacity > 0)
    {
        clone->items = xcalloc(redirs->capacity, sizeof(exec_redirection_t));
        if (!clone->items)
        {
            exec_redirections_destroy(&clone);
            return NULL;
        }
        clone->capacity = redirs->capacity;
    }

    /* Deep copy each redirection */
    for (size_t i = 0; i < redirs->count; i++)
    {
        const exec_redirection_t *src = &redirs->items[i];
        exec_redirection_t *dst = &clone->items[clone->count];

        /* Copy scalar fields */
        dst->type = src->type;
        dst->explicit_fd = src->explicit_fd;
        dst->target_kind = src->target_kind;
        dst->source_line = src->source_line;

        /* Deep copy io_location_varname if present */
        if (src->io_location_varname)
        {
            dst->io_location_varname = string_create_from(src->io_location_varname);
        }
        else
        {
            dst->io_location_varname = NULL;
        }

        /* Deep copy target data based on target_kind */
        switch (src->target_kind)
        {
        case REDIR_TARGET_FILE:
            if (src->target.file.filename)
            {
                dst->target.file.filename = string_create_from(src->target.file.filename);
            }
            else
            {
                dst->target.file.filename = NULL;
            }
            if (src->target.file.tok)
            {
                dst->target.file.tok = token_clone(src->target.file.tok);
            }
            else
            {
                dst->target.file.tok = NULL;
            }
            break;

        case REDIR_TARGET_FD:
            dst->target.fd.fixed_fd = src->target.fd.fixed_fd;
            if (src->target.fd.fd_expression)
            {
                dst->target.fd.fd_expression = string_create_from(src->target.fd.fd_expression);
            }
            else
            {
                dst->target.fd.fd_expression = NULL;
            }
            if (src->target.fd.fd_token)
            {
                dst->target.fd.fd_token = token_clone(src->target.fd.fd_token);
            }
            else
            {
                dst->target.fd.fd_token = NULL;
            }
            break;

        case REDIR_TARGET_BUFFER:
            if (src->target.heredoc.content)
            {
                dst->target.heredoc.content = string_create_from(src->target.heredoc.content);
            }
            else
            {
                dst->target.heredoc.content = NULL;
            }
            dst->target.heredoc.needs_expansion = src->target.heredoc.needs_expansion;
            break;

        case REDIR_TARGET_CLOSE:
        case REDIR_TARGET_FD_STRING:
        case REDIR_TARGET_INVALID:
        default:
            /* No additional data to copy */
            break;
        }

        clone->count++;
    }

    return clone;
}

/* ============================================================================
 * Runtime Redirection Wrapper (exec_redirections_t)
 * ============================================================================ */

/**
 * Apply redirections from the runtime structure.
 * Selects the appropriate platform-specific implementation.
 *
 * @param frame The execution frame

 * @param redirections The runtime redirection structure
 * @return 0 on success, -1 on error
 */
miga_exec_status_t exec_redirect_apply_redirectons(miga_frame_t *frame,
                                            const exec_redirections_t *redirections)
{
    miga_exec_status_t st = exec_apply_redirections_iso_c(frame, redirections);
    return st;
}

/* ============================================================================
 * AST to Runtime Conversion
 * ============================================================================ */

/**
 * Convert AST redirection nodes to runtime exec_redirections_t structure.
 * This happens once during command execution, converting from AST to runtime structures.
 *
 * @param frame The execution frame (for error reporting)
 * @param ast_redirs The AST redirection nodes
 * @return A new exec_redirections_t structure, or NULL on error
 */
exec_redirections_t *exec_redirections_create_from_ast_nodes(miga_frame_t *frame,
                                                             const ast_node_list_t *ast_redirs)
{
    if (!ast_redirs || ast_node_list_size(ast_redirs) == 0)
        return NULL;

    exec_redirections_t *redirs = exec_redirections_create();
    if (!redirs)
    {
        exec_set_error_cstr(frame->executor, "Failed to create redirection structure");
        return NULL;
    }

    int count = ast_node_list_size(ast_redirs);
    for (int i = 0; i < count; i++)
    {
        const ast_node_t *ast_redir = ast_node_list_get(ast_redirs, i);
        if (ast_redir->type != AST_REDIRECTION)
        {
            exec_set_error_cstr(frame->executor, "Expected AST_REDIRECTION node");
            exec_redirections_destroy(&redirs);
            return NULL;
        }

        /* Create a runtime redirection from the AST node */
        exec_redirection_t *runtime_redir = xcalloc(1, sizeof(exec_redirection_t));
        if (!runtime_redir)
        {
            exec_set_error_cstr(frame->executor, "Failed to allocate redirection structure");
            exec_redirections_destroy(&redirs);
            return NULL;
        }

        /* Access AST redirection data */
        const redirection_type_t ast_redir_type = ast_redir->data.redirection.redir_type;
        const int ast_io_number = ast_redir->data.redirection.io_number;
        const redir_target_kind_t ast_operand = ast_redir->data.redirection.operand;
        const bool ast_buffer_needs_expansion = ast_redir->data.redirection.buffer_needs_expansion;
        const token_t *ast_target = ast_redir->data.redirection.target;
        const string_t *ast_buffer = ast_redir->data.redirection.buffer;
        const string_t *ast_fd_string = ast_redir->data.redirection.fd_string;
        (void)ast_fd_string; /* used by the descriptor-based configurations only */

        runtime_redir->type = ast_redir_type;
        runtime_redir->explicit_fd = ast_io_number;
        runtime_redir->target_kind = ast_operand;

        /* Copy target information based on target kind */
        switch (ast_operand)
        {
        case REDIR_TARGET_FILE:
            /* For file target, we store the token (which contains parts).
             * At expansion time, we'll process these parts.
             * For now, store a simple copy of the filename if it's a simple literal.
             */
            if (ast_target && ast_target->parts && ast_target->parts->size > 0)
            {
                /* If all the parts are PART_LITERAL, let's construct the target.file.filename now.
                 */
                bool all_literal = true;
                for (int p = 0; p < ast_target->parts->size; p++)
                {
                    part_t *part = ast_target->parts->parts[p];
                    if (!part || part->type != PART_LITERAL || !part->text)
                    {
                        /* Not a simple literal - we'll need to expand at runtime */
                        all_literal = false;
                        break;
                    }
                }

                if (all_literal)
                {
                    /* All parts are literal - construct the filename now */
                    string_t *filename = string_create();
                    for (int p = 0; p < ast_target->parts->size; p++)
                    {
                        part_t *part = ast_target->parts->parts[p];
                        if (part && part->type == PART_LITERAL && part->text)
                        {
                            string_append(filename, part->text);
                        }
                    }
                    runtime_redir->target.file.filename = filename;
                    runtime_redir->target.file.is_expanded = true;
                }
                else
                {
                    runtime_redir->target.file.tok = token_clone(ast_target);
                    runtime_redir->target.file.is_expanded = false;
                }
            }
            break;

        case REDIR_TARGET_FD:
            /* For FD target, we can have a fixed fd, a literal, or an expression (including
             * variable). */
            runtime_redir->target.fd.fixed_fd = -1;
            if (ast_target && ast_target->type == TOKEN_IO_NUMBER && ast_target->io_number >= 0)
            {
                runtime_redir->target.fd.fixed_fd = ast_target->io_number;
            }
            else if (ast_target && ast_target->parts && ast_target->parts->size > 0)
            {
                bool all_literal = true;
                for (int p = 0; p < ast_target->parts->size; p++)
                {
                    part_t *part = ast_target->parts->parts[p];
                    if (!part || part->type != PART_LITERAL || !part->text)
                    {
                        all_literal = false;
                        break;
                    }
                }
                if (all_literal)
                {
                    // Concatenate all literal parts as fd_expression
                    string_t *fd_expr = string_create();
                    for (int p = 0; p < ast_target->parts->size; p++)
                    {
                        part_t *part = ast_target->parts->parts[p];
                        if (part && part->type == PART_LITERAL && part->text)
                            string_append(fd_expr, part->text);
                    }
                    runtime_redir->target.fd.fd_expression = fd_expr;
                }
                else
                {
                    // Contains non-literal (e.g., variable), store full token for later expansion
                    runtime_redir->target.fd.fd_token = token_clone(ast_target);
                }
            }
            break;

        case REDIR_TARGET_CLOSE:
            /* No target data needed */
            break;

        case REDIR_TARGET_BUFFER:
            if (ast_buffer)
            {
                runtime_redir->target.heredoc.content = string_create_from(ast_buffer);
            }
            runtime_redir->target.heredoc.needs_expansion = ast_buffer_needs_expansion;
            break;

        case REDIR_TARGET_INVALID:
        default:
            exec_set_error_cstr(frame->executor, "Invalid redirection target kind");
            xfree(runtime_redir);
            exec_redirections_destroy(&redirs);
            return NULL;
        }

        exec_redirections_append(redirs, runtime_redir);
        xfree(runtime_redir);
    }

    return redirs;
}
