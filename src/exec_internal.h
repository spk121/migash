#ifndef EXEC_INTERNAL_H
#define EXEC_INTERNAL_H

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
#include "func_store.h"
#include "job_store.h"
#include "positional_params.h"
#include "sig_act.h"
#include "string_t.h"
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

    /* Tokenizer (persistent across interactive commands) */
    struct tokenizer_t *tokenizer;

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
    exec_frame_t *top_frame;
    exec_frame_t *current_frame;
};

/* ============================================================================
 * Partial Execution State (concrete definition)
 * ============================================================================ */

/**
 * Concrete definition of exec_partial_state_t.
 *
 * Callers zero-initialise this before the first call to
 * exec_execute_command_string_partial().  The executor fills it with
 * continuation state after each call.
 */
struct exec_partial_state_t
{
    /* Whether we are mid-parse (e.g. inside an unclosed quote or compound
       command that spans multiple input chunks). */
    bool incomplete;

    /* Accumulated input from previous partial calls, if any. */
    string_t *accumulated_input;

    /* Source location tracking across calls. */
    string_t *filename;
    size_t line_number;

    /* Tokenizer / parser state carried across calls. */
    struct tokenizer_t *tokenizer;
    /* Additional parser continuation state may be added here. */
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================
 *
 * These functions are used by the executor, frame, and builtin dispatch
 * implementation files.  They must NOT be called by library consumers.
 */

/* ── AST visitor (internal) ──────────────────────────────────────────────── */

typedef bool (*ast_visitor_fn)(const ast_node_t *node, void *user_data);
bool ast_traverse(const ast_node_t *root, ast_visitor_fn visitor, void *user_data);

/* ── Command substitution callback (internal, for the expander) ──────────── */

string_t *exec_command_subst_callback(void *userdata, const string_t *command);

/* ── Direct store access (internal) ──────────────────────────────────────── */

positional_params_t *exec_get_positional_params(const exec_t *executor);
variable_store_t *exec_get_variables(const exec_t *executor);
alias_store_t *exec_get_aliases(const exec_t *executor);

/* ── Function call via AST node (internal) ───────────────────────────────── */

frame_status_t frame_set_function_ast(exec_frame_t *frame, const string_t *name,
                                      const ast_node_t *body);

frame_exec_status_t frame_call_function(exec_frame_t *frame, const string_t *name,
                                        const string_list_t *args);

/* ── Word-token expansion (internal — public API uses frame_expand_string) ─ */

string_list_t *frame_expand_word_token(exec_frame_t *frame, const token_t *tok);

/* ── Exit trap execution (internal) ──────────────────────────────────────── */

void frame_run_exit_traps(const trap_store_t *store, exec_frame_t *frame);

/* ── Top-frame lazy initialisation (internal) ────────────────────────────── */

bool exec_ensure_top_frame(exec_t *executor);

/* ── Status code translation helpers ─────────────────────────────────────── */

/**
 * Translate an internal var_store_error_t to the public frame_status_t.
 */
frame_status_t frame_status_from_var_error(var_store_error_t err);

/**
 * Translate an internal func_store_error_t to the public frame_status_t.
 */
frame_status_t frame_status_from_func_error(func_store_error_t err);

#endif /* EXEC_INTERNAL_H */
