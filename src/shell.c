#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "shell.h"

/* This is not part of libmigash, so only use the public API header. */
#include "libmigash.h"

/* FIXME: figure out logging */
#define log_fatal(...) while(false) {}

struct shell_t
{
    shell_mode_t mode;         // Interactive, non-interactive, etc.
    string_t *script_filename; // Script file name (if any)
    string_t *command_string;  // Command string (if any)
    miga_exec_t *executor;          // Points to the currently executing miga_exec_t
};

static sh_status_t shell_execute_script_file(shell_t *sh);
static sh_status_t shell_execute_interactive(shell_t *sh);
static sh_status_t shell_execute_command_string(shell_t *sh);
static sh_status_t shell_execute_stdin(shell_t *sh);

shell_t *shell_create(const shell_cfg_t *cfg)
{
    Expects_not_null(cfg);

    shell_t *sh = calloc(1, sizeof(shell_t));
    if (!sh)
        return NULL;

    sh->mode = cfg->mode;

    /* Initialize script filename if provided */
    if (cfg->command_file)
    {
        sh->script_filename = string_create_from_cstr(cfg->command_file);
    }
    else
    {
        sh->script_filename = NULL;
    }

    /* Initialize command string if provided */
    if (cfg->command_string)
    {
        sh->command_string = string_create_from_cstr(cfg->command_string);
    }
    else
    {
        sh->command_string = NULL;
    }

    sh->executor = exec_create();
    if (cfg->command_name)
        exec_set_shell_name_cstr(sh->executor, cfg->command_name);
    if (cfg->arguments)
        exec_set_args_cstr(sh->executor, cfg->argument_count, cfg->arguments);
    if (cfg->envp)
        exec_set_envp_cstr(sh->executor, cfg->envp);
    if (cfg->flags.allexport)
        exec_set_flag_allexport(sh->executor, true);
    if (cfg->flags.errexit)
        exec_set_flag_errexit(sh->executor, true);
    if (cfg->flags.ignoreeof)
        exec_set_flag_ignoreeof(sh->executor, true);
    //if (cfg->flags.monitor)
    //    exec_set_flag_monitor(sh->executor, true);
    if (cfg->flags.noclobber)
        exec_set_flag_noclobber(sh->executor, true);
    if (cfg->flags.noexec)
        exec_set_flag_noexec(sh->executor, true);
    if (cfg->flags.noglob)
        exec_set_flag_noglob(sh->executor, true);
    if (cfg->flags.nounset)
        exec_set_flag_nounset(sh->executor, true);
    if (cfg->flags.pipefail)
        exec_set_flag_pipefail(sh->executor, true);
    if (cfg->flags.verbose)
        exec_set_flag_verbose(sh->executor, true);
    if (cfg->flags.vi)
        exec_set_flag_vi(sh->executor, true);
    if (cfg->flags.xtrace)
        exec_set_flag_xtrace(sh->executor, true);

    return sh;
}

void shell_destroy(shell_t **sh)
{
    Expects_not_null(sh);
    if (!*sh)
        return;

    /* Clean up string fields */
    if ((*sh)->command_string)
    {
        string_destroy(&(*sh)->command_string);
    }
    if ((*sh)->script_filename)
    {
        string_destroy(&(*sh)->script_filename);
    }

    exec_destroy(&(*sh)->executor);
    free(*sh);
    *sh = NULL;
}

// Mode dispatch: Handle high-level logic, delegate executor calls
sh_status_t shell_execute(shell_t *sh)
{
    Expects_not_null(sh);
    switch (sh->mode)
    {
    case SHELL_MODE_SCRIPT_FILE:
        if (!sh->script_filename)
        {
            exec_set_error_cstr(sh->executor, "No script file specified");
            return SH_INTERNAL_ERROR;
        }
        return shell_execute_script_file(sh);
    case SHELL_MODE_INTERACTIVE:
        return shell_execute_interactive(sh);
    case SHELL_MODE_COMMAND_STRING:
        if (!sh->command_string)
        {
            exec_set_error_cstr(sh->executor, "No command string specified");
            return SH_INTERNAL_ERROR;
        }
        return shell_execute_command_string(sh);
    case SHELL_MODE_STDIN:
        exec_setup_interactive(sh->executor);
        return shell_execute_stdin(sh);
    case SHELL_MODE_UNKNOWN:
    case SHELL_MODE_INVALID_UID_GID:
    default:
        exec_set_error_cstr(sh->executor, "Invalid shell mode");
        return SH_INTERNAL_ERROR;
    }
}

void shell_cleanup(void *sh_ptr)
{
    if (!sh_ptr)
        return;
    shell_t *sh = (shell_t *)sh_ptr;

    if (sh->command_string)
    {
        string_destroy(&sh->command_string);
    }
    if (sh->script_filename)
    {
        string_destroy(&sh->script_filename);
    }

    // Clean up the executor
    exec_destroy(&sh->executor);

    // Free the shell structure
    free(sh);
}

// TODO: Implement these functions for REPL and script execution
sh_status_t shell_feed_line(shell_t *sh, const char *line, int line_num)
{
    Expects_not_null(sh);
    Expects_not_null(line);
    (void)line_num; // unused for now

    // TODO: Implement lexer -> parser -> executor pipeline
    // For now, just return OK
    return SH_OK;
}

static sh_status_t shell_execute_script_file(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->script_filename);

    FILE *fp = fopen(string_cstr(sh->script_filename), "r");
    if (!fp)
    {
        exec_set_error_printf(sh->executor, "Cannot open file: %s",
                              string_cstr(sh->script_filename));
        return SH_RUNTIME_ERROR;
    }

    miga_exec_status_t setup_status = exec_setup_noninteractive(sh->executor);
    if (setup_status != MIGA_EXEC_STATUS_OK)
    {
        fprintf(stderr, "Failed to parse RC file: %s\n", exec_get_error_cstr(sh->executor));
        // Not a fatal error.
    }

    /* Set $0 to the script filename */
    miga_frame_t *frame = exec_get_current_frame(sh->executor);
    frame_set_arg0(frame, sh->script_filename);

    miga_exec_status_t status = exec_execute_stream(sh->executor, fp);
    fclose(fp);

    // Convert miga_exec_status_t to sh_status_t
    switch (status)
    {
    case MIGA_EXEC_STATUS_OK:
        return SH_OK;
    case MIGA_EXEC_STATUS_ERROR:
        return SH_RUNTIME_ERROR;
    case MIGA_EXEC_STATUS_NOT_IMPL:
    default:
        return SH_INTERNAL_ERROR;
    }
}

sh_status_t shell_execute_command_string(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->command_string);

    const char *cmd = string_cstr(sh->command_string);

    miga_exec_result_t result = exec_execute_command_string(sh->executor, cmd);

    switch (result.status)
    {
    case MIGA_EXEC_STATUS_OK:
        return SH_OK;
    case MIGA_EXEC_STATUS_ERROR:
        return SH_RUNTIME_ERROR;
    case MIGA_EXEC_STATUS_NOT_IMPL:
    default:
        return SH_INTERNAL_ERROR;
    }
}

static sh_status_t shell_execute_interactive(shell_t *sh)
{
    Expects_not_null(sh);

    exec_setup_interactive(sh->executor);
    miga_exec_status_t status = exec_execute_stream(sh->executor, stdin);

    switch (status)
    {
    case MIGA_EXEC_STATUS_OK:
        return SH_OK;
    case MIGA_EXEC_STATUS_ERROR:
        return SH_RUNTIME_ERROR;
    case MIGA_EXEC_STATUS_NOT_IMPL:
    default:
        return SH_INTERNAL_ERROR;
    }
}

sh_status_t shell_execute_stdin(shell_t *sh)
{
    Expects_not_null(sh);
    miga_exec_status_t status = exec_execute_stream(sh->executor, stdin);
    // Convert miga_exec_status_t to sh_status_t
    switch (status)
    {
    case MIGA_EXEC_STATUS_OK:
        return SH_OK;
    case MIGA_EXEC_STATUS_ERROR:
        return SH_RUNTIME_ERROR;
    case MIGA_EXEC_STATUS_NOT_IMPL:
    default:
        return SH_INTERNAL_ERROR;
    }
}

const char *shell_last_error(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    return exec_get_error_cstr(sh->executor);
}

void shell_reset_error(shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    exec_clear_error(sh->executor);
}

sh_status_t shell_run_script(shell_t *sh, const char *script)
{
    Expects_not_null(sh);
    Expects_not_null(script);

    // TODO: Implement script execution
    // Parse entire script buffer and execute
    return SH_OK;
}

const char *shell_get_ps1(const shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    return exec_get_ps1_cstr(sh->executor);
}

const char *shell_get_ps2(const shell_t *sh)
{
    Expects_not_null(sh);
    Expects_not_null(sh->executor);

    return exec_get_ps2_cstr(sh->executor);
}

miga_exec_t *shell_get_exec(shell_t *sh)
{
    Expects_not_null(sh);
    return sh->executor;
}
