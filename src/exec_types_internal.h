#ifndef EXEC_TYPES_INTERNAL_H
#define EXEC_TYPES_INTERNAL_H

/**
 * @file exec_internal.h
 * @brief Internal definitions for the shell executor.
 *
 * This header contains the concrete struct definitions for exec_t and
 * exec_frame_t, as well as internal-only functions that are used by the
 * executor implementation and must NOT be used by library consumers or
 * builtin implementations.
 *
 * Library consumers should include only exec.h and frame.h.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

/* ── Public API headers (for the types they define) ──────────────────────── */
#include "exec.h"
#include "frame.h"

/* ── Internal module headers ─────────────────────────────────────────────── */
#include "alias_store.h"
#include "ast.h"
#include "builtin_store.h" /* New: registry of builtin_fn_t entries    */
#include "exec_expander.h"
#include "exec_frame.h"
#include "exec_parse_session.h"
#include "func_store.h"
#include "job_store.h"
#include "lexer_t.h"
#include "positional_params.h"
#include "sig_act.h"
#include "string_list.h"
#include "string_t.h"
#include "tokenizer.h"
#include "trap_store.h"
#include "variable_store.h"

#ifdef POSIX_API
#include <sys/resource.h>
#include <sys/types.h>
#endif

#if defined(POSIX_API) || defined(UCRT_API)
#include "fd_table.h"
#endif

/* ============================================================================
 * Platform Constants
 * ============================================================================ */

#ifndef NSIG
#ifdef _NSIG
#define NSIG _NSIG
#else
#define NSIG 64
#endif
#endif

#ifdef POSIX_API
#define EXEC_SYSTEM_RC_PATH "/etc/mgshrc"
#define EXEC_USER_RC_NAME ".mgshrc"
#define EXEC_RC_IN_XDG_CONFIG_HOME
#elif defined(UCRT_API)
#define EXEC_USER_RC_NAME "mgshrc"
#define EXEC_RC_IN_LOCAL_APP_DATA
#else
#define EXEC_USER_RC_NAME "MGSH.RC"
#define EXEC_RC_IN_CURRENT_DIRECTORY
#endif

#define EXEC_CFG_FALLBACK_ARGV0 "mgsh"

/* ============================================================================
 * Option Flags (concrete definition)
 * ============================================================================ */

struct exec_opt_flags_t
{
    bool allexport; /* set -a */
    bool errexit;   /* set -e */
    bool ignoreeof; /* set -I */
    bool noclobber; /* set -C */
    bool noglob;    /* set -f */
    bool noexec;    /* set -n */
    bool nounset;   /* set -u */
    bool pipefail;  /* set -o pipefail */
    bool verbose;   /* set -v */
    bool vi;        /* set -o vi */
    bool xtrace;    /* set -x */
};

typedef struct exec_opt_flags_t exec_opt_flags_t;

#define EXEC_OPT_FLAGS_INIT                                                                        \
    {                                                                                              \
        .allexport = false,                                                                        \
        .errexit = false,                                                                          \
        .ignoreeof = false,                                                                        \
        .noclobber = false,                                                                        \
        .noglob = false,                                                                           \
        .noexec = false,                                                                           \
        .nounset = false,                                                                          \
        .pipefail = false,                                                                         \
        .verbose = false,                                                                          \
        .vi = false,                                                                               \
        .xtrace = false,                                                                           \
    }

/* ============================================================================
 * Executor State (concrete definition of exec_t)
 * ============================================================================ */

struct exec_t
{
    /* ─── Singleton state ─────────────────────────────────────────────── */

    bool shell_pid_valid;
    bool shell_ppid_valid;
#ifdef POSIX_API
    pid_t shell_pid;
    pid_t shell_ppid;
#else
    int shell_pid;
    int shell_ppid;
#endif

    bool is_interactive;
    bool is_login_shell;

    bool signals_installed;
    sig_act_store_t *original_signals;
    volatile sig_atomic_t sigint_received;
    volatile sig_atomic_t sigchld_received;
    volatile sig_atomic_t trap_pending[NSIG];

    job_store_t *jobs;
    bool job_control_disabled;

    bool pgid_valid;
#ifdef POSIX_API
    pid_t pgid;
#else
    int pgid;
#endif

    /* Pipeline status (pipefail / PIPESTATUS) */
    int *pipe_statuses;
    size_t pipe_status_count;
    size_t pipe_status_capacity;

    /* Error state */
    string_t *error_msg;

    /* Exit request (set by exec_request_exit, checked by the main loop) */
    bool exit_requested;

    /* Builtin registry */
    builtin_store_t *builtins;

    /* ─── Top-frame initialisation data ───────────────────────────────── */

    int argc;
    char **argv;
    char **envp;

    string_t *shell_name;
    string_list_t *shell_args;
    string_list_t *env_vars;

    exec_opt_flags_t opt;

    bool rc_loaded;
    bool rc_files_sourced;
    bool inhibit_rc_files;
    string_t *system_rc_filename;
    string_t *user_rc_filename;

    /* Top-frame stores (owned until top frame is created) */
    variable_store_t *variables;
    variable_store_t *local_variables;
    positional_params_t *positional_params;
    func_store_t *functions;
    alias_store_t *aliases;
    trap_store_t *traps;

    /* Parse session (persistent across interactive commands) */
    exec_parse_session_t *session;

#if defined(POSIX_API) || defined(UCRT_API)
    fd_table_t *open_fds;
    int next_fd;
#endif

    string_t *working_directory;
#ifdef POSIX_API
    mode_t umask;
    rlim_t file_size_limit;
#else
    int umask;
#endif

    int last_exit_status;
    bool last_exit_status_set;
    int last_background_pid;
    bool last_background_pid_set;
    string_t *last_argument;
    bool last_argument_set;

    /* ─── Frame stack ─────────────────────────────────────────────────── */

    bool top_frame_initialized;
    struct exec_frame_t *top_frame;
    struct exec_frame_t *current_frame;
};

/**
 * Types of redirection operations.
 */

/* redirection_type_t is recycled from the one in ast.h */

/**
 * How the target of a redirection is specified.
 */

/* redir_target_kind_t is recycled from the one in ast.h */

/**
 * Runtime representation of a single redirection.
 */
typedef struct exec_redirection_t
{
    redirection_type_t type;
    int explicit_fd;     /* [n] prefix, or -1 for default */
    bool is_io_location; /* POSIX 2024 {varname} syntax */
    string_t *io_location_varname;
    redir_target_kind_t target_kind;

    union {
        /* REDIR_TARGET_FILE */
        struct
        {
            bool is_expanded;
            string_t *filename; /* Expanded filename */
            token_t *tok;       /* Original parts for expansion (if not yet expanded) */
        } file;

        /* REDIR_TARGET_FD */
        struct
        {
            int fixed_fd;            /* Literal fd number, or -1 */
            string_t *fd_expression; /* If fd comes from expansion */
            token_t *fd_token;       /* Full token for variable-derived FDs */
        } fd;

        /* REDIR_TARGET_HEREDOC */
        struct
        {
            string_t *content;    /* The heredoc content */
            bool needs_expansion; /* false if delimiter was quoted */
        } heredoc;

        /* REDIR_TARGET_IO_LOCATION */
        struct
        {
            string_t *raw_filename; /* For {var}>file */
            int fixed_fd;           /* For {var}>&N */
        } io_location;
    } target;

    int source_line; /* For error messages */
} exec_redirection_t;

/**
 * Dynamic array of runtime redirections.
 */

typedef struct exec_redirections_t
{
    exec_redirection_t *items;
    size_t count;
    size_t capacity;
} exec_redirections_t;

/* ============================================================================
 * Partial Execution State
 * ============================================================================
 *
 * exec_partial_state_t is now a typedef for exec_parse_session_t.
 * The concrete definition lives in exec_parse_session.h.
 *
 * exec_string_ctx_t has been eliminated; its fields are now part of
 * exec_parse_session_t.
 * ============================================================================ */

/* ============================================================================
 * Frame-Level Execution Result Types
 * ============================================================================ */

// todo:
// exec_frame_execute_status_t is a simple enum
// exec_frame_execute_result_t is a struct that includes the status plus any additional info (like
//   exit status, flow control info, etc)

typedef enum exec_frame_execute_status_t
{
    EXEC_FRAME_EXECUTE_STATUS_OK,
    EXEC_FRAME_EXECUTE_STATUS_ERROR,
    EXEC_FRAME_EXECUTE_STATUS_NOT_IMPL,
    EXEC_FRAME_EXECUTE_STATUS_INCOMPLETE,
    EXEC_FRAME_EXECUTE_STATUS_UNKNOWN
} exec_frame_execute_status_t;

// FIXME? TODO? is exec_control_flow_t a frame-level concept?
// or should it remain at the exec level?

typedef struct exec_frame_execute_result_t
{
    enum exec_frame_execute_status_t status;

    bool has_exit_status;
    int exit_status; // valid if has_exit_status is true

    bool has_control_flow;
    enum exec_control_flow_t
        flow;       // for break/continue/return: what control flow is pending from this execution
    int flow_depth; // for break/continue: how many nested loops to break/continue out of
} exec_frame_execute_result_t;

#endif /* EXEC_TYPES_INTERNAL_H */
