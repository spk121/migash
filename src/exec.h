#ifndef EXECUTOR_H
#define EXECUTOR_H

// TODO: this header needs to be reworked so that it only contains the public
// API for the top-level executor, and all internal definitions are moved to exec_internal.h or
// similar.
//
// Also any frame-level APIs in this header should be moved to frame.h, which is the public
// API for execution frames. Together, exec.h and frame.h should be the only public headers
// for the execution engine, and shell.c and builtins.c should only be using the public APIs defined
// in those headers.
//
// Aside from string_t.h and string_list.h,
// no other internal API should be exposed in this header.

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

#include "alias_store.h"
#include "ast.h"
// #include "exec_command.h"
#include "exec_expander.h"
#include "exec_frame.h"
#include "func_store.h"
#include "job_store.h"
#include "positional_params.h"
#include "sig_act.h"
#include "string_t.h"
#include "string_list.h"
#include "trap_store.h"
#include "variable_store.h"

#ifdef POSIX_API
#include <sys/types.h>
#include <sys/resource.h>
#endif

#if defined(POSIX_API) || defined(UCRT_API)
#include "fd_table.h"
#endif

#ifdef POSIX_API
#define EXEC_SYSTEM_RC_PATH "/etc/mgshrc"
#define EXEC_USER_RC_NAME ".mgshrc"
#define EXEC_RC_IN_XDG_CONFIG_HOME
#elifdef UCRT_API
#define EXEC_USER_RC_NAME "mgshrc"
#define EXEC_RC_IN_LOCAL_APP_DATA
#else
// ISO C cannot rely on any paths existing, or on any filename conventions.
// So fallback to presuming FAT-12 style names being valid (a practical worst case)
// and no directory structure.
#define EXEC_USER_RC_NAME "MGSH.RC"
#define EXEC_RC_IN_CURRENT_DIRECTORY
#endif

#ifndef NSIG
#ifdef _NSIG
#define NSIG _NSIG
#else
#define NSIG 64 /* A common value for NSIG when not defined by the system */
#endif
#endif

/* ============================================================================
 * Executor State (Singleton)
 * ============================================================================ */

struct exec_opt_flags_t
{
    bool allexport; // set -a
    bool errexit;   // set -e
    bool ignoreeof; // set -I
    bool noclobber; // set -C
    bool noglob;    // set -f
    bool noexec;    // set -n
    bool nounset;   // set -u
    bool pipefail;  // set -o pipefail
    bool verbose;   // set -v
    bool vi;        // set -o vi
    bool xtrace;    // set -x
};


#define EXEC_CFG_FALLBACK_ARGV0 "mgsh"
#define EXEC_CFG_FALLBACK_OPT_FLAGS_INIT \
    {                                      \
        .allexport = false,                \
        .errexit = false,                  \
        .ignoreeof = false,                \
        .noclobber = false,                \
        .noglob = false,                   \
        .noexec = false,                   \
        .nounset = false,                  \
        .pipefail = false,                 \
        .verbose = false,                  \
        .vi = false,                       \
        .xtrace = false,                   \
    }

/**
 * Holds override info for initializing an executor.
 * For information not provided,
 * info from POSIX, UCRT, or ISO C API calls will be used,
 * or as a last resort, hardcoded FALLBACK values.
 */
struct exec_cfg_t
{
    /* Start-up Environment */
    bool argv_set;
    int argc; /* May be zero. */
    char *const *argv; /* NULL iff argc is zero. Fallback will be used for argv[0]. */

    bool envp_set;
    char *const *envp; /* If !envp_set, this is NULL and envp will come from getopt(). */

    /* Shell identity overrides */
    bool shell_name_set;
    const char *shell_name; /* Overrides argv[0]-derived shell name */

    bool shell_args_set;
    string_list_t *shell_args; /* Overrides argv[1..] derived args */

    bool env_vars_set;
    string_list_t *env_vars; /* Overrides envp-derived environment list */

    /* Flags */
    bool opt_flags_set;
    struct exec_opt_flags_t opt; /* If !opt_flags_set, fallback is used. */

    bool is_interactive_set;
    bool is_interactive;
    bool is_login_shell_set;
    bool is_login_shell;

    bool job_control_enabled_set;
    bool job_control_enabled;

    /* Working directory override */
    bool working_directory_set;
    const char *working_directory;

    /* File permissions */
    bool umask_set;
#ifdef POSIX_API
    mode_t umask;
    bool file_size_limit_set;
    rlim_t file_size_limit;
#else
    int umask;
#endif

    /* Special parameters */
    bool last_exit_status_set;
    int last_exit_status;
    bool last_background_pid_set;
    int last_background_pid;
    bool last_argument_set;
    const char *last_argument;

    /* Process group / PID overrides */
    bool pgid_set;
#ifdef POSIX_API
    pid_t pgid;
#else
    int pgid;
#endif
    bool pgid_valid_set;
    bool pgid_valid;

    bool shell_pid_set;
#ifdef POSIX_API
    pid_t shell_pid;
#else
    int shell_pid;
#endif
    bool shell_pid_valid_set;
    bool shell_pid_valid;

    bool shell_ppid_set;
#ifdef POSIX_API
    pid_t shell_ppid;
#else
    int shell_ppid;
#endif
    bool shell_ppid_valid_set;
    bool shell_ppid_valid;

    /* RC file state overrides */
    bool rc_loaded_set;
    bool rc_loaded;
    bool rc_files_sourced_set;
    bool rc_files_sourced;
};

/**
 * The exec_t holds global shell state that persists across all frames.
 */
struct exec_t
{
    /* -------------------------------------------------------------------------
     * 1) Singleton executor state (not per-frame)
     * -------------------------------------------------------------------------
     */
    bool shell_pid_valid;
    bool shell_ppid_valid;
#ifdef POSIX_API
    pid_t shell_pid;  /* $$ - PID of the main shell process */
    pid_t shell_ppid; /* $PPID at startup */
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
    bool job_control_enabled;

    bool pgid_valid;
#ifdef POSIX_API
    pid_t pgid; /* Shell's process group ID */
#else
    int pgid;
#endif

    /* Pipeline status (pipefail / PIPESTATUS) */
    int *pipe_statuses;
    size_t pipe_status_count;
    size_t pipe_status_capacity;

    /* Error state */
    string_t *error_msg;

    /* -------------------------------------------------------------------------
     * 2) Top-frame initialization data
     * -------------------------------------------------------------------------
     * This data is used to initialize the top frame when it is created lazily.
     * The top frame and current frame remain NULL until first execution.
     */
    int argc;
    char **argv;
    char **envp;

    string_t *shell_name;      /* $0 for top-level (argv[0] or script name) */
    string_list_t *shell_args; /* $@ for top-level (argv[1..argc-1]) */
    string_list_t *env_vars;   /* Environment variables */

    // TODO: initialize this in-place in the top-level frame instead of building it here and then
    // moving ownership to the frame on initialization.
    struct exec_opt_flags_t opt;

    bool rc_loaded;
    bool rc_files_sourced;

    // TODO: to avoid leaking API details, we should just build these into
    // the top-level frame. We can add a flag to the frame initialization
    // that indicates whether the top-level frame has finished initialization,
    // and if not, the frame can look for these exec_t fields to initialize its state.
    /* Top-frame stores (owned until top frame is created) */
    variable_store_t *variables;
    variable_store_t *local_variables; /* Unused for top frame; reserved for parity */
    positional_params_t *positional_params;
    func_store_t *functions;
    alias_store_t *aliases;
    trap_store_t *traps;

    /* Tokenizer for persistent state across interactive commands */
    // TODO: make this opaque. It is singleton, so it does belong in exec_t,
    // but we don't want to expose the full tokenizer API in this header since it's really only used
    // internally by the executor.
    struct tokenizer_t *tokenizer;

    // TODO: initialize this in-place in the top-level frame instead of building it here and then
    // moving ownership to the frame on initialization.
#if defined(POSIX_API) || defined(UCRT_API)
    fd_table_t *open_fds;
    int next_fd;
#endif

    // TODO: initialize this in-place in the top-level frame instead of building it here and then
    // moving ownership to the frame on initialization.
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

    /* -------------------------------------------------------------------------
     * 3) Frame stack pointers
     * -------------------------------------------------------------------------
     */

    // TODO: before this flag indicated both that top_frame is not null and is initialized. Now
    // top_frame can be non-null but not completely initialized and ready for the first execution, so this
    // flag only indicates whether the top frame has been fully initialized with the initial state
    // and is ready for execution
    bool top_frame_initialized;
    exec_frame_t *top_frame;
    exec_frame_t *current_frame;
};

typedef struct exec_opt_flags_t exec_opt_flags_t;
typedef struct exec_cfg_t exec_cfg_t;
typedef struct exec_t exec_t;

/* ============================================================================
 * Executor Context
 * ============================================================================ */

#define EXEC_RESULT_T_DEFINED
typedef enum exec_status_t
{
    EXEC_OK = 0,
    EXEC_ERROR = 1,
    EXEC_NOT_IMPL = 2,
    EXEC_OK_INTERNAL_FUNCTION_STORED,
    EXEC_BREAK,           ///< break statement executed
    EXEC_CONTINUE,        ///< continue statement executed
    EXEC_RETURN,          ///< return statement executed
    EXEC_EXIT,            ///< exit statement executed
    EXEC_INCOMPLETE_INPUT, ///< input ended but command was incomplete (e.g., unclosed quotes)
} exec_status_t;

/* ============================================================================
 * Executor Configuration Functions
 * ============================================================================ */

// FIXME: this is a really messy API and needs to be reworked. The idea is that the shell will call
// this function to populate an exec_cfg_t with any overrides for the initial state of the executor.
// Perhaps it would be better to have separate functions for each category of overrides (e.g., one
// for shell identity, one for flags, etc.) rather than one big function that takes everything at
// once. Or maybe we can have a builder pattern where the shell calls individual setter functions on
// the exec_cfg_t to set the desired overrides, and then passes the fully built config to
// exec_create. The current API is a bit too monolithic and makes it hard to understand which parts
// are actually needed and which are optional.
void exec_cfg_set_from_shell_options(exec_cfg_t *cfg,
    int argc, char *const *argv, char *const *envp,
    const char *shell_name,
    string_list_t *shell_args,
    string_list_t *env_vars,
    const exec_opt_flags_t *opt_flags,
    bool is_interactive,
    bool is_login_shell,
    bool job_control_enabled);

/* ============================================================================
 * Executor Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new executor.
 */
// TODO: make this take just `exec_t *exec_create(void)`, and have the shell call separate config
// functions on the executor after creation but before execution starts. The current API is a bit too
// monolithic and makes it hard to understand which parts are actually needed and which are
// optional.
exec_t *exec_create(const struct exec_cfg_t *cfg);

/**
 * Destroy an executor and free all associated memory.
 * Safe to call with nullptr.
 */
void exec_destroy(exec_t **executor);

/* ============================================================================
 * Getters and Setters
 * ============================================================================ */

// TODO: we need getters / setters for all the init state previously described
// in exec_cfg_t. The shell needs to be able to query this state when initializing the top frame.
// Some of these will update the top-level exec_t, and some will update the top frame,
// lazily creating it if necessary. The ones that update the top-level frame need to
// error if execution has already begun.
// These fields include:
// - argc / argv / envp
// - shell name / args
// - env vars
// - flags (allexport, errexit, etc.)
// - interactive / login shell flags
// - job control enabled flags
// - working directory
// - file permissions (umask, file size limit)
// - process group / PID overrides (pgid, shell pid/ppid)
// but not RC files. Those are handled specially by the setup functions,
// because they require a mostly initialized top-level frame to be sourced.

/**
 * Getters and setters for the initial state of the executor. These are used to configure the
 * executor before execution begins, and they may only be called before the top frame is
 * initialized. After execution begins, these setter functions will return false to indicate that the state
 * cannot be modified.
 * 
 * When RC parsing is allowed, the values set by these functions may be overridden.
 */

bool exec_is_args_set(const exec_t *executor);
char *const *exec_get_args(const exec_t *executor, int *argc_out);
bool exec_set_args(exec_t *executor, int argc, char *const *argv);

bool exec_is_envp_set(const exec_t *executor);
char *const *exec_get_envp(const exec_t *executor);
bool exec_set_envp(exec_t *executor,
                   char *const *envp); // NULL-terminated list of "VAR=VALUE" strings

bool exec_is_shell_name_set(const exec_t *executor);
const char *exec_get_shell_name(const exec_t *executor);
bool exec_set_shell_name(
    exec_t *executor,
    const char *shell_name); // Sets the shell name (used for $0 and in error messages). This is
                             // usually derived from argv[0], but this allows the shell to override
                             // it if needed (e.g., for login shells or when argv[0] is not
                             // meaningful).

bool exec_get_flag_allexport(const exec_t *executor);
bool exec_set_flag_allexport(exec_t *executor, bool value);

bool exec_get_flag_errexit(const exec_t *executor);
bool exec_set_flag_errexit(exec_t *executor, bool value);

bool exec_get_flag_ignoreeof(const exec_t *executor);
bool exec_set_flag_ignoreeof(exec_t *executor, bool value);

bool exec_get_flag_noclobber(const exec_t *executor);
bool exec_set_flag_noclobber(exec_t *executor, bool value);

bool exec_get_flag_noglob(const exec_t *executor);
bool exec_set_flag_noglob(exec_t *executor, bool value);

bool exec_get_flag_noexec(const exec_t *executor);
bool exec_set_flag_noexec(exec_t *executor, bool value);

bool exec_get_flag_nounset(const exec_t *executor);
bool exec_set_flag_nounset(exec_t *executor, bool value);

bool exec_get_flag_pipefail(const exec_t *executor);
bool exec_set_flag_pipefail(exec_t *executor, bool value);

bool exec_get_flag_verbose(const exec_t *executor);
bool exec_set_flag_verbose(exec_t *executor, bool value);

bool exec_get_flag_vi(const exec_t *executor);
bool exec_set_flag_vi(exec_t *executor, bool value);

bool exec_get_flag_xtrace(const exec_t *executor);
bool exec_set_flag_xtrace(exec_t *executor, bool value);

// RC files will normally be sourced during setup for interactive shells and login shells,
// and will be inhibited for non-interactive non-login shells, but the shell can override this if
// needed using these getters/setters.
bool exec_get_inhibit_rc_files(const exec_t *executor);
bool exec_set_inhibit_rc_files(exec_t *executor, bool inhibit);

bool exec_is_system_rc_filename_set(const exec_t *executor);
const char *exec_get_system_rc_filename(const exec_t *executor);
// It is unspecified what occurs if this is not a absolute path.
bool exec_set_system_rc_filename(exec_t *executor, const char *filename);

bool exec_is_user_rc_filename_set(const exec_t *executor);
const char *exec_get_user_rc_filename(const exec_t *executor);
// May be absolute, relative (which means relative to the CWD),
// begin with a ~ to indicate relative to the home directory,
// or be a simple filename that is looked up in the CWD.
// In ISO C mode, the tilde will be treated as a literal character.
bool exec_set_user_rc_filename(exec_t *executor, const char *filename);

bool exec_get_is_login_shell(const exec_t *executor);
bool exec_set_is_login_shell(
    exec_t *executor, bool is_login_shell); // Sets whether the shell is a login shell. This is
                                            // usually determined at startup based on argv[0], but
                                            // this allows the shell to override it if needed.

bool exec_get_job_control_enabled(const exec_t *executor);
bool exec_set_job_control_enabled(exec_t *executor, bool job_control_enabled);

bool exec_is_working_directory_set(const exec_t *executor);
const char *exec_get_working_directory(const exec_t *executor);
bool exec_set_working_directory(exec_t *executor, const char *working_directory);

bool exec_is_umask_set(const exec_t *executor);
int exec_get_umask(const exec_t *executor);
bool exec_set_umask(exec_t *executor, int umask);
#ifdef POSIX_API
#define exec_is_umask_posix_set(ex) exec_is_umask_set(x)
mode_t exec_get_umask_posix(const exec_t *executor);
bool exec_set_umask_posix(exec_t *executor, mode_t umask);

bool exec_is_file_size_limit_set(const exec_t *executor);
rlim_t exec_get_file_size_limit(exec_t *executor);
bool exec_set_file_size_limit(exec_t *executor, rlim_t file_size_limit);
#endif
bool exec_is_process_group_set(const exec_t *executor);
int exec_get_process_group(const exec_t *executor);
bool exec_set_process_group(exec_t *executor, int pgid);

bool exec_is_shell_pid_set(const exec_t *executor);
int exec_get_shell_pid(const exec_t *executor);
bool exec_set_shell_pid(exec_t *executor, int shell_pid);

bool exec_is_shell_ppid_set(const exec_t *executor);
// Returns -1 if not set or invalid. Note that a valid shell PPID may also be -1 if the shell is PID
// 1 or if the parent process has exited, so the caller should check exec_is_shell_ppid_set() before
// relying on the value returned by this function.
int exec_get_shell_ppid(const exec_t *executor);
bool exec_set_shell_ppid(exec_t *executor, int shell_ppid);


// TODO: we need a getter that indicates if the exec_t has been initialized
// with the top frame yet by checking the `top_frame_initialized` flag.
// This is important because some of the state in exec_t is only used for
// initializing the top frame, and once the top frame is initialized, that state is no longer
// relevant and the config setters will have no effect.
bool exec_is_top_frame_initialized(const exec_t *executor);

// TODO: we a getter for getting the current frame.
frame_t *exec_get_current_frame(const exec_t *executor);

// TODO: make getters / setters for pipe statuses, which are global state.
// No setter since the shell shouldn't be modifying these directly, but the executor needs to be
// able to update them after executing pipelines, and the shell needs to be able to read them for
// PIPESTATUS and for implementing `set -o pipefail`.
int exec_get_pipe_status_count(const exec_t *executor);
const int *exec_get_pipe_statuses(const exec_t *executor);
void exec_reset_pipe_statuses(exec_t *executor);

/* ============================================================================
 * Execution Functions
 * ============================================================================ */

/**
 * Setup the executor for interactive execution, including sourcing rc files.
 * This should be called before calling an executor like exec_execute_stream() if the shell is running in interactive mode.
 * This will perform tasks such as installing signal handlers, sourcing the system and user rc
 * files.
 * If the top-frame has not been initialized yet, this function will initialize it with the
 * appropriate state for interactive execution.
 * 
 * Note that even if this function returns EXEC_ERROR, the executor will still be in a usable
 * state, but it may not have sourced the rc files, so the shell may want to print a warning or
 * error message.
 * 
 * Returns EXEC_OK on success, EXEC_ERROR on failure (e.g., if sourcing rc files fails).
 * It may also return EXEC_ERROR if the executor was already initialized with a top frame.
 */
exec_status_t exec_setup_interactive_execute(exec_t *executor);

/**
 * Setup the executor for non-interactive execution, including sourcing the system rc file if it's a
 * login shell. This should be called before exec_execute_stream() if the shell is running in
 * non-interactive mode.
 *
 * Note that even if this function returns EXEC_ERROR, the executor will still be in a usable
 * state, but it may not have sourced the rc files, so the shell may want to print a warning or
 * error message.
 * 
 * Returns EXEC_OK on success, EXEC_ERROR on failure (e.g., if sourcing rc files fails).
 * It may also return EXEC_ERROR if the executor was already initialized with a top frame.
 */
exec_status_t exec_setup_non_interactive_execute(exec_t *executor);

/* N.B. If execution begins without exec_setup_interactive_execute()
 * or exec_setup_non_interactive_execute() being called, the executor will perform lazy
 * initialization, but, it will not source any rc files. Other initialization tasks may also
 * be skipped, so it's recommended to call one of the setup functions.
 */

/**
 * Execute commands from a stream (file or stdin).
 * Reads lines from the stream, parses them, and executes them.
 * If the exec has been set up to interactive mode by calling
 * `exec_setup_interactive_execute()`, this function will provide
 * the full REPL experience, including printing prompts and handling signals.
 *
 * @param executor The executor context
 * @param fp The file stream to read from
 * @return EXEC_OK on success, EXEC_ERROR on error, EXEC_RETURN if a return statement was executed
 * at top-level, EXEC_EXIT if an exit statement was executed at top-level,
 * EXEC_INCOMPLETE_INPUT if the input was incomplete or on EOF.
 */
exec_status_t exec_execute_stream(exec_t *executor, FILE *fp);

/* Like exec_execute_stream(), but allows specifying a filename for error reporting.
 * This is intended for non-interactive use cases where the input is coming from a file or string
 * rather than a terminal, so the filename can be used in error messages instead of "stdin".
 * 
 * Calling this function for an interactive or login session will work, but may create
 * confusion since the prompts and error messages will refer to the specified filename rather than
 * "stdin".
 */
void exec_execute_stream_with_filename(exec_t *executor, FILE *fp, const char *filename);

/**
 * Status returned by a line-editor callback.
 * Negative values are fatal (EOF or unrecoverable error → usually stop REPL)
 * Zero or positive = success (and possibly carry extra semantic information)
 */
typedef enum
{
    LINE_EDIT_OK = 0,            /* success, no special meaning */
    LINE_EDIT_EOF = -1,          /* EOF / ctrl-D / closed input */
    LINE_EDIT_ERROR = -2,        /* fatal I/O or allocation error */
    LINE_EDIT_INTERRUPT = -3,    /* SIGINT received → usually SIGINT handling in shell */
    LINE_EDIT_PREVIOUS = -4,     /* user requested previous history entry (e.g., up arrow) */
    LINE_EDIT_NEXT = -5,         /* user requested next history entry (e.g., down arrow) */
    LINE_EDIT_CURRENT = -6,      /* user requested current history entry (e.g., right arrow on empty line) */
    LINE_EDIT_HISTORY_IDX = 1000 /* values >= 1000 may be used for history event numbers */
    /* caller can do: if (ret >= LINE_EDIT_HISTORY_IDX) history_event = ret - LINE_EDIT_HISTORY_IDX;
     */
} line_edit_status_t;

/**
 * Signature of a user-provided line editor function.
 *
 * @param prompt        NUL-terminated prompt string (caller owns it, may be NULL/empty)
 * @param line_out      On entry:
 *                        - may be NULL   → you must allocate a new string_t* and store it here
 *                        - may be non-NULL → existing (possibly empty) string_t*; you may
 *                          replace it, append to it, or leave it unchanged.
 *                      On success you must ensure *line_out points to a valid heap-allocated
 *                      string_t containing the final line (without the trailing newline).
 *                      On failure (*line_out may be freed/reset or left alone — caller checks
 *                       status)
 * @param user_data     Opaque pointer passed through from exec_execute_stream_with_line_editor()
 *
 * @return              LINE_EDIT_OK (0) on success
 *                      LINE_EDIT_EOF (-1) on clean EOF
 *                      LINE_EDIT_ERROR (-2) on fatal error
 *                      LINE_EDIT_INTERRUPT (-3) on SIGINT / user cancel
 *                      LINE_EDIT_PREVIOUS (-4) on user request for previous history entry
 *                      LINE_EDIT_NEXT (-5) on user request for next history entry
 *                      LINE_EDIT_CURRENT (-6) on user request for current history entry
 *                      positive value >= LINE_EDIT_HISTORY_IDX when the returned line comes
 *                      from history (allows shell to perform history expansion correctly)
 */
typedef line_edit_status_t (*line_editor_fn_t)(const char *prompt, string_t **line_out,
                                               void *user_data);

/**
 * Execute commands from a stream with a custom line editor.
 *
 * Only valid in interactive mode (must have called exec_setup_interactive_execute() before).
 * If fp is a terminal (isatty(fileno(fp))) then the line editor is expected to handle
 * raw terminal mode, echoing, signals, etc. If fp is not a terminal, most custom editors
 * will fall back to plain fgets()-like behavior.
 *
 * @param executor              executor context
 * @param fp                    input stream (stdin most of the time in interactive use)
 * @param line_editor_fn        callback that reads one line (with editing)
 * @param line_editor_user_data opaque data passed to every invocation of line_editor_fn
 *
 * @return
 *   EXEC_OK                 - successful execution of commands
 *   EXEC_ERROR              - general error (incl. line editor returning LINE_EDIT_ERROR)
 *   EXEC_INCOMPLETE_INPUT   - unexpected EOF / incomplete continuation line
 *   EXEC_RETURN             - top-level return
 *   EXEC_EXIT               - top-level exit
 *   EXEC_SIGNALED           - SIGINT or similar caused early termination
 */
exec_status_t exec_execute_stream_with_line_editor(exec_t *executor, FILE *fp,
                                                   line_editor_fn_t line_editor_fn,
                                                   void *line_editor_user_data);

/**
 * Execute a complete command string at top-level. 
 *
 * This function executes a self-contained command string at top-level,
 * and it is specifically intended for use for '-c' command-line arguments.
 * Unlike exec_execute_stream(), this function expects the command to be
 * complete and will treat incomplete input (unclosed quotes, missing keywords) as an error.
 * 
 * It does not print prompts or handle signals, since it's not intended for interactive use.
 * It does not print errors to the output; instead, it returns an error status and sets the
 * executor's error message.
 *
 * @param command The complete command string to execute
 * @return exec_result_t with execution status and exit code
 */
// TODO: disambiguate this from the frame-level command execution function.
// Clarify that this is *only* for executing complete command strings at top-level,
// and is not the correct API for executing strings generally.
exec_result_t exec_execute_command_string(exec_t *executor, const char *command);

// If a user of the shell library wants to implement their own repl, we'll
// need a command that can handle incomplete input to return a state that indicates that more
// input is needed, and that also allows the caller to provide a filename and line number
// for the current string.
// The caller can memset the partial state to zero to clear it.
typedef struct exec_partial_state_t exec_partial_state_t;
// Caller provides a pointer to the opaque partial state struct, and this function
// will fill it with information about the state of the execution after an attempt to execute
// the command string
// If it was incomplete, the partial state will be populated with information about what
// was parsed successfully for the caller to use in the next call to continue execution.
//
// A line_number of less than 1 indicates that no line information is provided.
// If the line_number is >= 1, then line_number (and, if non-NULL, the filename) will
// be used in error messages and
// for $LINENO. If a filename is provided but the line_number < 1, the filename will be ignored.
// Once a filename and line number has been provided, they will be stored in the partial
// state and used for subsequent calls until the partial state is memset to zero or
// the filename/line number is updated by the caller.
//
exec_status_t exec_execute_command_string_partial(exec_t *executor, const char *command,
                                                  const char *filename, size_t line_number,
                                                  exec_partial_state_t *partial_state_out);

/**
 * Execute a simple command.
 */
// TODO: remove this function and replace all call sites with exec_command_string, which is more
// general and can handle any command string, not just simple commands. The only reason to keep this
// would be if we want to preserve the ability to execute a pre-parsed AST node for a simple
// command, but that seems like an internal API that the shell should not be using directly, and we
// can always add an internal helper function
// exec_status_t exec_execute_simple_command(exec_frame_t *frame, const ast_node_t *node);

/**
 * Check if any background jobs have completed, and if so mark them done
 * in the job store.
 * Only properly functions in POSIX_API mode.
 * In other modes, this is a no-op.
 * If notify is true, print completed jobs to the output.
 */
void exec_reap_background_jobs(exec_t *executor, bool notify);

// TODO: add a full set of background jobs APIs here, such as listing jobs, bringing jobs to
// foreground/background, sending signals to jobs, etc. The job store should be encapsulated behind
// these APIs so that the shell doesn't need to access it directly.
// The builtins will need an API for 'bg', 'fg', 'jobs', and 'kill' builtins, and the shell will
// need an API for printing job status in the prompt and for implementing `set -o notify`.
// 
// size_t job_store_count(const job_store_t *store);
size_t exec_get_job_count(const exec_t *executor);
// int job_store_get_job_ids(const job_store_t *store, int *job_ids, size_t max_jobs);

// Remember
// job_id: an integer that represents a job that can have multiple processes (e.g., a pipeline).
// This is what the shell uses to refer to jobs in job control commands like `fg %1` or `kill %2`.
// pid: the actual process ID of a single process. A job can have multiple pids if it has multiple
// processes.
// (UCRT ONLY) handle: a Windows HANDLE that represents a process. This is used in UCRT mode instead
// of pid for job control, since Windows doesn't have real PIDs or process groups. Since we
// use _spawnvpe to launch processes in UCRT mode, no job_id will have more than one PID or HANDLE.
// (ISO_C ONLY) since we only use `system()` to execute command, which runs synchronously and doesn't
// give any job control capabilities, in ISO_C there will never be any job_ids, and exec_get_job_count()
// will always return zero.



// =====================
// Public Job API for shell builtins (bg, fg, jobs, kill)
// =====================
#ifdef POSIX_API
#include <sys/types.h>
#endif

// Job state (mirrors job_state_t)
typedef enum exec_job_state_t {
    EXEC_JOB_RUNNING,
    EXEC_JOB_STOPPED,
    EXEC_JOB_DONE,
    EXEC_JOB_TERMINATED
} exec_job_state_t;

// Get the number of jobs
size_t exec_get_job_count(const exec_t *executor);

// Get a list of active job IDs (returns number written)
size_t exec_get_job_ids(const exec_t *executor, int *job_ids, size_t max_jobs);

// Get the current (%%) and previous (%-) job IDs
int exec_get_current_job_id(const exec_t *executor);
int exec_get_previous_job_id(const exec_t *executor);

// Get job state, command line, and background/foreground status
exec_job_state_t exec_job_get_state(const exec_t *executor, int job_id);
const char *exec_job_get_command(const exec_t *executor, int job_id);
bool exec_job_is_background(const exec_t *executor, int job_id);

// Get job's process group ID (for fg/bg/kill)
#ifdef POSIX_API
pid_t exec_job_get_pgid(const exec_t *executor, int job_id);
#else
int exec_job_get_pgid(const exec_t *executor, int job_id);
#endif


// Iterate over processes in a job
size_t exec_job_get_process_count(const exec_t *executor, int job_id);
#ifdef POSIX_API
pid_t exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index);
#elif defined(UCRT_API)
int exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index);
uintptr_t exec_job_get_process_handle(const exec_t *executor, int job_id, size_t index);
#else
int exec_job_get_process_pid(const exec_t *executor, int job_id, size_t index);
#endif
exec_job_state_t exec_job_get_process_state(const exec_t *executor, int job_id, size_t index);
int exec_job_get_process_exit_status(const exec_t *executor, int job_id, size_t index);

// Bring a job to the foreground/background
bool exec_job_foreground(exec_t *executor, int job_id, bool cont);
bool exec_job_background(exec_t *executor, int job_id, bool cont);

// Send a signal to a job (all processes in the job)
bool exec_job_kill(exec_t *executor, int job_id, int sig);

// Print jobs status (for 'jobs' builtin)
void exec_print_jobs(const exec_t *executor, FILE *output);




/* ============================================================================
 * Visitor Pattern Support
 * ============================================================================ */

/**
 * Visitor callback function type.
 * Returns true to continue traversal, false to stop.
 */
// TODO: this is an internal API that should not be exposed in this header. The shell should not be
// using this directly. We can move this to exec_internal.h and make it clear that it's only for
// internal use.
typedef bool (*ast_visitor_fn)(const ast_node_t *node, void *user_data);

/**
 * Traverse an AST in pre-order, calling the visitor function for each node.
 *
 * @param root The root node to start traversal from
 * @param visitor The visitor function to call for each node
 * @param user_data User data to pass to the visitor function
 *
 * @return true if traversal completed, false if stopped early
 */
// TODO: this is an internal API that should not be exposed in this header. The shell should not be
// using this directly. We can move this to exec_internal.h and make it clear that it's only for
// internal use.
bool ast_traverse(const ast_node_t *root, ast_visitor_fn visitor, void *user_data);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get the last exit status.
 */
int exec_get_exit_status(const exec_t *executor);

/**
 * Set the exit status.
 */
void exec_set_exit_status(exec_t *executor, int status);

/**
 * Get the error message from the last failed operation.
 * Returns NULL if no error.
 */
const char *exec_get_error(const exec_t *executor);

/**
 * Set an error message.
 */
void exec_set_error(exec_t *executor, const char *format, ...);

/**
 * Clear the error state.
 */
void exec_clear_error(exec_t *executor);

/**
 * Get the PS1 prompt string.
 * 
 * If a client is implementing their own REPL using the shell library,
 * they can use this function to get the PS1 prompt string from the variable store.
 * Returns the value of the PS1 variable from the variable store,
 * or a default prompt if PS1 is not set. Caller frees the returned string.
 *
 * @param executor The executor context
 * @return The PS1 prompt string (never NULL)
 */
char *exec_get_ps1(const exec_t *executor);

/**
 * @brief Returns a newly allocated string containing the rendered PS1 prompt, with all expansions
 * performed in accordance with the current frame's state. The caller is responsible for freeing the
 * returned string.
 * @param executor The executor context
 * @return A newly allocated string containing the rendered PS1 prompt (caller must free)
 */
char *exec_get_rendered_ps1(const exec_t *executor);

/**
 * Get the PS2 prompt string.
 * Returns the value of the PS2 variable from the variable store,
 * or a default prompt if PS2 is not set. Caller frees the returned string.
 * Note that PS2 is never rendered with expansions.
 *
 * @param executor The executor context
 * @return The PS2 prompt string (never NULL)
 */
char *exec_get_ps2(const exec_t *executor);

// TODO: this is an internal API and should not be exposed in this header.
// The shell should not be using this directly. We can move this to exec_internal.h and make it
// clear that it's only for internal use.
// These types: positional_params_t, variable_store_t, and alias_store_t are all internal types that
// should not be exposed in this header. We can move them to exec_internal.h and make it clear that
// they're only for internal use.
positional_params_t *exec_get_positional_params(const exec_t *executor);
variable_store_t *exec_get_variables(const exec_t *executor);
alias_store_t *exec_get_aliases(const exec_t *executor);

/**
 * Check if the executor is running in interactive mode.
 */
bool exec_is_interactive(const exec_t *executor);

/**
 * Check if the executor is running as a login shell.
 * This is determined at executor initialization based on argv[0].
 */
bool exec_is_login_shell(const exec_t *executor);

/* ============================================================================
 * Expander Callbacks
 * ============================================================================ */

/**
 * Command substitution callback for the expander.
 * Executes a command and returns its output.
 *
 * @param command The command string to execute
 * @param userdata Pointer to the executor context
 * @param command The command to execute
 * @return The output of the command as a newly allocated string_t (caller must free),
 *         or NULL on error
 */
// TODO: this is an internal API and should not be exposed in this header. The shell should not be
// using this directly. We can move this to exec_internal.h and make it clear that it's only for
// internal use.
string_t *exec_command_subst_callback(void *userdata, const string_t *command);

/**
 * Get positional parameters from executor (for expander).
 */
// TODO: this is an internal API and should not be exposed in this header. The shell should not be
// using this directly. We can move this to exec_internal.h and make it clear that it's only for
// internal use.
positional_params_t *exec_get_positional_params(const exec_t *executor);

/**
 * Get variable store from executor (for expander).
 */
// TODO: this is an internal API and should not be exposed in this header. The shell should not be
// using this directly. We can move this to exec_internal.h and make it clear that it's only for
// internal use.
variable_store_t *exec_get_variables(const exec_t *executor);

#endif

