// ============================================================================
// trap_store.c
// ============================================================================
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <signal.h>
#include <string.h>

#include "trap_store.h"

#include "logging.h"
#include "sig_act.h"
#include "miga/xalloc.h"

// Platform-specific signal support gates
#ifdef TRAP_USE_POSIX_SIGNALS
// POSIX signals: ABRT, ALRM, BUS, CHLD, CONT, FPE, HUP, ILL, INT, PIPE,
//                QUIT, SEGV, TERM, TSTP, TTIN, TTOU, USR1, USR2, WINCH
#define TRAP_SIGNAL_COUNT 19

static inline int get_max_signal_number(void)
{
    int max = SIGABRT;
#ifdef SIGALRM
    if (SIGALRM > max)
        max = SIGALRM;
#endif
#ifdef SIGBUS
    if (SIGBUS > max)
        max = SIGBUS;
#endif
#ifdef SIGCHLD
    if (SIGCHLD > max)
        max = SIGCHLD;
#endif
#ifdef SIGCONT
    if (SIGCONT > max)
        max = SIGCONT;
#endif
    if (SIGFPE > max)
        max = SIGFPE;
#ifdef SIGHUP
    if (SIGHUP > max)
        max = SIGHUP;
#endif
    if (SIGILL > max)
        max = SIGILL;
    if (SIGINT > max)
        max = SIGINT;
#ifdef SIGPIPE
    if (SIGPIPE > max)
        max = SIGPIPE;
#endif
#ifdef SIGQUIT
    if (SIGQUIT > max)
        max = SIGQUIT;
#endif
    if (SIGSEGV > max)
        max = SIGSEGV;
    if (SIGTERM > max)
        max = SIGTERM;
#ifdef SIGTSTP
    if (SIGTSTP > max)
        max = SIGTSTP;
#endif
#ifdef SIGTTIN
    if (SIGTTIN > max)
        max = SIGTTIN;
#endif
#ifdef SIGTTOU
    if (SIGTTOU > max)
        max = SIGTTOU;
#endif
#ifdef SIGUSR1
    if (SIGUSR1 > max)
        max = SIGUSR1;
#endif
#ifdef SIGUSR2
    if (SIGUSR2 > max)
        max = SIGUSR2;
#endif
#ifdef SIGWINCH
    if (SIGWINCH > max)
        max = SIGWINCH;
#endif
    return max;
}

#elifdef TRAP_USE_UCRT_SIGNALS
// UCRT signals: ABRT, FPE, ILL, INT, SEGV, TERM
#define TRAP_SIGNAL_COUNT 6

static inline int get_max_signal_number(void)
{
    int max = SIGABRT;
    if (SIGFPE > max)
        max = SIGFPE;
    if (SIGILL > max)
        max = SIGILL;
    if (SIGINT > max)
        max = SIGINT;
    if (SIGSEGV > max)
        max = SIGSEGV;
    if (SIGTERM > max)
        max = SIGTERM;
    return max;
}

#else
// ISO C signals: ABRT, FPE, ILL, INT, SEGV, TERM
#define TRAP_SIGNAL_COUNT 6

static inline int get_max_signal_number(void)
{
    int max = SIGABRT;
    if (SIGFPE > max)
        max = SIGFPE;
    if (SIGILL > max)
        max = SIGILL;
    if (SIGINT > max)
        max = SIGINT;
    if (SIGSEGV > max)
        max = SIGSEGV;
    if (SIGTERM > max)
        max = SIGTERM;
    return max;
}
#endif

// ============================================================================
// Create/Destroy Functions
// ============================================================================

trap_store_t *trap_store_create(void)
{
    trap_store_t *store = xcalloc(1, sizeof(trap_store_t));

    // Allocate array large enough to index by signal number
    // (signal numbers are not necessarily contiguous, so we need space
    // up to the maximum signal number)
    int max_signo = get_max_signal_number();
    store->capacity = (size_t)(max_signo + 1);
    store->traps = xcalloc(store->capacity, sizeof(trap_action_t));

    // Initialize all traps as default (not set)
    for (size_t i = 0; i < store->capacity; i++)
    {
        store->traps[i].signal_number = (int)i;
        store->traps[i].action = NULL;
        store->traps[i].is_ignored = false;
        store->traps[i].is_default = true;
    }

    store->exit_trap_set = false;
    store->exit_action = NULL;

    return store;
}

void trap_store_destroy(trap_store_t **store_ptr)
{
    Expects_not_null(store_ptr);
    Expects_not_null(*store_ptr);

    trap_store_t *store = *store_ptr;

    // Free all trap action strings
    for (size_t i = 0; i < store->capacity; i++)
    {
        if (store->traps[i].action)
            string_destroy(&store->traps[i].action);
    }

    xfree(store->traps);

    if (store->exit_action)
        string_destroy(&store->exit_action);

    xfree(store);
    *store_ptr = NULL;
}

trap_store_t *trap_store_clone(const trap_store_t *store)
{
    Expects_not_null(store);

    trap_store_t *copy = trap_store_create();

    // Copy all trap actions
    for (size_t i = 0; i < store->capacity && i < copy->capacity; i++)
    {
        copy->traps[i].signal_number = store->traps[i].signal_number;
        copy->traps[i].action = store->traps[i].action ? string_create_from(store->traps[i].action) : NULL;
        copy->traps[i].is_ignored = store->traps[i].is_ignored;
        copy->traps[i].is_default = store->traps[i].is_default;
    }

    // Copy EXIT trap
    copy->exit_trap_set = store->exit_trap_set;
    copy->exit_action = store->exit_action ? string_create_from(store->exit_action) : NULL;

    return copy;
}

static trap_store_t *signal_handler_trap_store = NULL;
static sig_act_store_t *signal_handler_sig_act_store = NULL;

void trap_store_set_current(trap_store_t* store)
{
    Expects_not_null(store);
    signal_handler_trap_store = store;
}

trap_store_t *trap_store_get_current(void)
{
    return signal_handler_trap_store;
}

void trap_store_set_sig_act_store(sig_act_store_t *store)
{
    signal_handler_sig_act_store = store;
}

sig_act_store_t *trap_store_get_sig_act_store(void)
{
    return signal_handler_sig_act_store;
}

// ============================================================================
// Trap Execution Functions
// ============================================================================

int execute_trap_action(const trap_action_t *trap)
{
    /* Trap actions are run by the executor (exec_run_pending_traps), never
     * from the signal handler. Kept for the trap_controller API. */
    (void)trap;
    return 0;
}

/* ----------------------------------------------------------------------------
 * Pending-signal flags. The only thing a signal handler may do is set a flag
 * (sig_atomic_t stores are async-signal-safe); the shell checks the flags
 * between commands and runs the trap actions then (XCU 2.11: "the trap
 * associated with that signal shall not be executed until after the
 * foreground command has completed").
 * ------------------------------------------------------------------------- */
static volatile sig_atomic_t *g_pending_flags = NULL;
static int g_pending_count = 0;
static volatile sig_atomic_t g_any_pending = 0;

void trap_store_set_pending_flags(volatile sig_atomic_t *flags, int count)
{
    g_pending_flags = flags;
    g_pending_count = count;
}

bool trap_store_any_pending(void)
{
    return g_any_pending != 0;
}

void trap_store_clear_any_pending(void)
{
    g_any_pending = 0;
}

static void note_pending(int signal_number)
{
    if (g_pending_flags && signal_number > 0 && signal_number < g_pending_count)
    {
        g_pending_flags[signal_number] = 1;
        g_any_pending = 1;
    }
}

#ifdef TRAP_USE_POSIX_SIGNALS
void trap_handler(int signal, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;
    note_pending(signal);
}
#elifdef TRAP_USE_UCRT_SIGNALS
int trap_handler(int signo, int fpe_code)
{
    (void)fpe_code;
    note_pending(signo);
    /* signal() handlers are one-shot on UCRT: re-arm */
    signal(signo, (void (*)(int))trap_handler);
    return 0;
}
#else
void trap_handler(int signo)
{
    note_pending(signo);
    /* ISO C does not say whether the disposition is reset: re-arm */
    signal(signo, trap_handler);
}
#endif

// ============================================================================
// Set/Get Functions
// ============================================================================

static bool is_non_catchable_signal(int signal_number)
{
    // SIGKILL and SIGSTOP cannot be caught or ignored
#ifdef SIGKILL
    if (signal_number == SIGKILL)
        return true;
#endif
#ifdef SIGSTOP
    if (signal_number == SIGSTOP)
        return true;
#endif
    return false;
}

bool trap_store_set(trap_store_t *store, int signal_number, string_t *action, bool is_ignored,
                    bool is_default)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);
    // SIGKILL and SIGSTOP cannot be caught
#ifdef SIGKILL
    if (signal_number == SIGKILL)
        return false;
#endif
#ifdef SIGSTOP
    if (signal_number == SIGSTOP)
        return false;
#endif

    // EXIT trap (signal 0) is not a real signal, doesn't need handler installation
    if (signal_number == 0)
    {
        return trap_store_set_exit(store, action, is_ignored, is_default);
    }


    trap_action_t *trap = &store->traps[signal_number];

    /* POSIX: signals that were ignored on entry to a non-interactive shell
     * cannot be trapped or reset. Accept the command, change nothing. */
    if (trap->ignored_on_entry)
        return true;

    // Free old action if it exists
    if (trap->action)
        string_destroy(&trap->action);

    trap->signal_number = signal_number;
    trap->action = action ? string_create_from(action) : NULL;
    trap->is_ignored = is_ignored;
    trap->is_default = is_default;

    // Install the signal handler or restore default/ignore disposition.
    // The first change of a signal's disposition remembers what the shell
    // had (its interactive handler, SIG_IGN, SIG_DFL); "trap - SIG" restores
    // that rather than SIG_DFL.
#ifdef TRAP_USE_POSIX_SIGNALS
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask);
        if (is_default)
        {
            if (trap->has_saved)
            {
                trap->has_saved = false;
                if (sigaction(signal_number, &trap->saved_action, NULL) == -1)
                    return false;
            }
            else
            {
                sa.sa_handler = SIG_DFL;
                if (sigaction(signal_number, &sa, NULL) == -1)
                    return false;
            }
            return true;
        }
        if (is_ignored)
            sa.sa_handler = SIG_IGN;
        else if (trap->action)
        {
            sa.sa_sigaction = trap_handler;
            sa.sa_flags = SA_SIGINFO | SA_RESTART;
        }
        else
            return true; /* nothing to install */
        struct sigaction previous;
        if (sigaction(signal_number, &sa, &previous) == -1)
            return false;
        if (!trap->has_saved)
        {
            trap->saved_action = previous;
            trap->has_saved = true;
        }
    }
#else
    // UCRT and ISO C use signal()
    if (is_default)
    {
        void (*restore)(int) = trap->has_saved ? trap->saved_handler : SIG_DFL;
        trap->has_saved = false;
        if (signal(signal_number, restore) == SIG_ERR)
            return false;
        return true;
    }
    void (*handler)(int) = NULL;
    if (is_ignored)
        handler = SIG_IGN;
    else if (trap->action)
        handler = (void (*)(int))trap_handler;
    else
        return true;
    void (*previous)(int) = signal(signal_number, handler);
    if (previous == SIG_ERR)
        return false;
    if (!trap->has_saved)
    {
        trap->saved_handler = previous;
        trap->has_saved = true;
    }
#endif

    return true; // Success
}

bool trap_store_set_exit(trap_store_t *store, string_t *action, bool is_ignored, bool is_default)
{
    Expects_not_null(store);

    if (store->exit_action)
        string_destroy(&store->exit_action);
    store->exit_action = NULL;

    if (is_default)
    {
        /* trap - EXIT: no EXIT trap */
        store->exit_trap_set = false;
        return true;
    }

    /* trap '' EXIT sets an empty action: nothing runs at exit */
    store->exit_trap_set = true;
    store->exit_action = (action && !is_ignored) ? string_create_from(action)
                                                 : string_create_from_cstr("");
    return true;
}

const trap_action_t *trap_store_get(const trap_store_t *store, int signal_number)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);

    const trap_action_t *trap = &store->traps[signal_number];

    // Only return if trap is actually set (not default)
    if (trap->is_default && !trap->is_ignored && !trap->action)
        return NULL;

    return trap;
}

const string_t *trap_store_get_exit(const trap_store_t *store)
{
    Expects_not_null(store);
    if (!store->exit_trap_set)
        return NULL;
    return store->exit_action;
}

#ifdef TRAP_USE_UCRT_SIGNALS
const trap_action_t *trap_store_get_fpe(const trap_store_t *store, int fpe_code)
{
    Expects_not_null(store);

    /* FPE trap simplification: UCRT SIGFPE handler can receive different
     * exception type codes (FPE_INTDIV, FPE_INTOVF, FPE_FLTDIV, FPE_FLTOVF,
     * FPE_FLTUND, FPE_FLTRES, FPE_FLTINV). However, since shell arithmetic
     * is performed on 'long' integers and not floating-point, FPE exceptions
     * should be rare or impossible. We store and return a single FPE trap
     * action for all exception types.
     *
     * The fpe_code parameter is accepted but currently ignored, allowing for
     * future per-exception-type handling if needed.
     * (fpe_code examples: FPE_INTDIV, FPE_INTOVF, FPE_FLTDIV, FPE_FLTOVF, etc.)
     */
    (void)fpe_code; // Suppress unused parameter warning

    trap_action_t *trap = &store->fpe_trap;

    // Only return if FPE trap is actually set (not default)
    if (trap->is_default && !trap->is_ignored && !trap->action)
        return NULL;

    return trap;
}
#endif

bool trap_store_is_set(const trap_store_t *store, int signal_number)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);

    const trap_action_t *trap = &store->traps[signal_number];
    return !trap->is_default || trap->is_ignored || trap->action != NULL;
}

bool trap_store_is_exit_set(const trap_store_t *store)
{
    Expects_not_null(store);
    return store->exit_trap_set;
}

void trap_store_clear(trap_store_t *store, int signal_number)
{
    Expects_not_null(store);
    Expects_ge(signal_number, 0);
    Expects_lt(signal_number, (int)store->capacity);

    // EXIT trap (signal 0) is not a real signal
    if (signal_number == 0)
    {
        trap_store_clear_exit(store);
        return;
    }

    trap_action_t *trap = &store->traps[signal_number];

    if (trap->action)
        string_destroy(&trap->action);

    trap->action = NULL;
    trap->is_ignored = false;
    trap->is_default = true;

    // Restore the disposition the shell had before the trap
#ifdef TRAP_USE_POSIX_SIGNALS
    if (trap->has_saved)
        sigaction(signal_number, &trap->saved_action, NULL);
    else
        signal(signal_number, SIG_DFL);
#else
    signal(signal_number, trap->has_saved ? trap->saved_handler : SIG_DFL);
#endif
    trap->has_saved = false;
}

void trap_store_clear_exit(trap_store_t *store)
{
    Expects_not_null(store);

    if (store->exit_action)
        string_destroy(&store->exit_action);

    store->exit_trap_set = false;
    store->exit_action = NULL;
}

// ============================================================================
// Signal Name Conversion
// ============================================================================

int trap_signal_name_to_number(const char *name)
{
    Expects_not_null(name);

    // Handle special case: EXIT (signal 0)
    if (strcmp(name, "EXIT") == 0)
        return 0;

    // ISO C signals (available on all platforms)
    if (strcmp(name, "ABRT") == 0)
        return SIGABRT;
    if (strcmp(name, "FPE") == 0)
        return SIGFPE;
    if (strcmp(name, "ILL") == 0)
        return SIGILL;
    if (strcmp(name, "INT") == 0)
        return SIGINT;
    if (strcmp(name, "SEGV") == 0)
        return SIGSEGV;
    if (strcmp(name, "TERM") == 0)
        return SIGTERM;

#ifdef TRAP_USE_POSIX_SIGNALS
    // POSIX-specific signals
    if (strcmp(name, "ALRM") == 0)
#ifdef SIGALRM
        return SIGALRM;
#else
        return -1;
#endif
    if (strcmp(name, "BUS") == 0)
#ifdef SIGBUS
        return SIGBUS;
#else
        return -1;
#endif
    if (strcmp(name, "CHLD") == 0)
#ifdef SIGCHLD
        return SIGCHLD;
#else
        return -1;
#endif
    if (strcmp(name, "CONT") == 0)
#ifdef SIGCONT
        return SIGCONT;
#else
        return -1;
#endif
    if (strcmp(name, "HUP") == 0)
#ifdef SIGHUP
        return SIGHUP;
#else
        return -1;
#endif
    if (strcmp(name, "KILL") == 0)
#ifdef SIGKILL
        return SIGKILL; // Not catchable, but recognized
#else
        return -1;
#endif
    if (strcmp(name, "PIPE") == 0)
#ifdef SIGPIPE
        return SIGPIPE;
#else
        return -1;
#endif
    if (strcmp(name, "QUIT") == 0)
#ifdef SIGQUIT
        return SIGQUIT;
#else
        return -1;
#endif
    if (strcmp(name, "STOP") == 0)
#ifdef SIGSTOP
        return SIGSTOP; // Not catchable, but recognized
#else
        return -1;
#endif
    if (strcmp(name, "TSTP") == 0)
#ifdef SIGTSTP
        return SIGTSTP;
#else
        return -1;
#endif
    if (strcmp(name, "TTIN") == 0)
#ifdef SIGTTIN
        return SIGTTIN;
#else
        return -1;
#endif
    if (strcmp(name, "TTOU") == 0)
#ifdef SIGTTOU
        return SIGTTOU;
#else
        return -1;
#endif
    if (strcmp(name, "USR1") == 0)
#ifdef SIGUSR1
        return SIGUSR1;
#else
        return -1;
#endif
    if (strcmp(name, "USR2") == 0)
#ifdef SIGUSR2
        return SIGUSR2;
#else
        return -1;
#endif

#ifdef SIGWINCH
    if (strcmp(name, "WINCH") == 0)
        return SIGWINCH;
#endif
#endif

    // Signal name not recognized
    return -1;
}

// Helper to check if a signal name is a known signal (regardless of platform support)
// Returns true if the name is recognized as a valid signal name
static bool is_known_signal_name(const char *name)
{
    Expects_not_null(name);

    // Handle special case: EXIT (signal 0)
    if (strcmp(name, "EXIT") == 0)
        return true;

    // ISO C signals (always known)
    if (strcmp(name, "ABRT") == 0 || strcmp(name, "FPE") == 0 ||
        strcmp(name, "ILL") == 0 || strcmp(name, "INT") == 0 ||
        strcmp(name, "SEGV") == 0 || strcmp(name, "TERM") == 0)
        return true;

#ifdef TRAP_USE_POSIX_SIGNALS
    // POSIX signals (known on POSIX platforms)
    if (strcmp(name, "ALRM") == 0 || strcmp(name, "BUS") == 0 ||
        strcmp(name, "CHLD") == 0 || strcmp(name, "CONT") == 0 ||
        strcmp(name, "HUP") == 0 || strcmp(name, "KILL") == 0 ||
        strcmp(name, "PIPE") == 0 || strcmp(name, "QUIT") == 0 ||
        strcmp(name, "STOP") == 0 || strcmp(name, "TSTP") == 0 ||
        strcmp(name, "TTIN") == 0 || strcmp(name, "TTOU") == 0 ||
        strcmp(name, "USR1") == 0 || strcmp(name, "USR2") == 0 ||
        strcmp(name, "WINCH") == 0)
        return true;
#endif

    return false;
}

bool trap_signal_name_is_invalid(const char *name)
{
    Expects_not_null(name);
    return !is_known_signal_name(name);
}

bool trap_signal_name_is_unsupported(const char *name)
{
    Expects_not_null(name);

    // If it's invalid, it's not "unsupported" - it's invalid
    if (!is_known_signal_name(name))
        return false;

    // If it's known, check if it's actually available on this platform
    // If trap_signal_name_to_number returns -1, it's unsupported
    // But we need to exclude EXIT (signal 0) which always succeeds
    if (strcmp(name, "EXIT") == 0)
        return false; // EXIT is always supported

    // For all other known signals, check if they're available on this platform
    return trap_signal_name_to_number(name) == -1;
}

const char *trap_signal_number_to_name(int signo)
{
    Expects_ge(signo, 0);  // Signal numbers must be non-negative (0 for EXIT, >0 for signals)

    // Handle special case: EXIT (signal 0)
    if (signo == 0)
        return "EXIT";

    // ISO C signals (available on all platforms)
    if (signo == SIGABRT)
        return "ABRT";
    if (signo == SIGFPE)
        return "FPE";
    if (signo == SIGILL)
        return "ILL";
    if (signo == SIGINT)
        return "INT";
    if (signo == SIGSEGV)
        return "SEGV";
    if (signo == SIGTERM)
        return "TERM";

#ifdef TRAP_USE_POSIX_SIGNALS
    // POSIX-specific signals
#ifdef SIGALRM
    if (signo == SIGALRM)
        return "ALRM";
#endif
#ifdef SIGBUS
    if (signo == SIGBUS)
        return "BUS";
#endif
#ifdef SIGCHLD
    if (signo == SIGCHLD)
        return "CHLD";
#endif
#ifdef SIGCONT
    if (signo == SIGCONT)
        return "CONT";
#endif
#ifdef SIGHUP
    if (signo == SIGHUP)
        return "HUP";
#endif
#ifdef SIGKILL
    if (signo == SIGKILL)
        return "KILL";
#endif
#ifdef SIGPIPE
    if (signo == SIGPIPE)
        return "PIPE";
#endif
#ifdef SIGQUIT
    if (signo == SIGQUIT)
        return "QUIT";
#endif
#ifdef SIGSTOP
    if (signo == SIGSTOP)
        return "STOP";
#endif
#ifdef SIGTSTP
    if (signo == SIGTSTP)
        return "TSTP";
#endif
#ifdef SIGTTIN
    if (signo == SIGTTIN)
        return "TTIN";
#endif
#ifdef SIGTTOU
    if (signo == SIGTTOU)
        return "TTOU";
#endif
#ifdef SIGUSR1
    if (signo == SIGUSR1)
        return "USR1";
#endif
#ifdef SIGUSR2
    if (signo == SIGUSR2)
        return "USR2";
#endif
#ifdef SIGWINCH
    if (signo == SIGWINCH)
        return "WINCH";
#endif
#endif

    // Signal number is not recognized in the available signal set
    // Check if it's a known signal on any platform but unsupported here
    // We need to check if it's a known ISO C signal that we couldn't match above
    // (This shouldn't happen as we check all ISO C signals)

    // Check for known POSIX signals that might not be available on this platform
#ifdef TRAP_USE_POSIX_SIGNALS
    // If we're on POSIX, all POSIX signals should have been checked above
    return "INVALID";
#else
    // On non-POSIX platforms, check if this is a known POSIX signal
    // If so, it's unsupported; otherwise it's invalid

    // Create a temporary name variable for the check
    // We'll use a heuristic: known POSIX signals have specific numbers
    // But without platform definitions, we can't reliably detect them
    // So we return "UNSUPPORTED" for any unmatched signal to be conservative
    // This handles the case where a POSIX system compiled on UCRT might have
    // signal numbers that don't map to our static list

    // Actually, a safer approach: if the signal number falls in a plausible range,
    // it might be unsupported; otherwise it's invalid
    if (signo > 0 && signo < 64)
        return "UNSUPPORTED"; // Plausible signal number, but not recognized
    else
        return "INVALID"; // Out of typical signal range
#endif
}

void trap_store_reset_non_ignored(trap_store_t* store)
{
    Expects_not_null(store);
    /* XCU 2.12: on entering a subshell, traps that are not being ignored are
     * set to the default actions. That includes the EXIT trap. */
    for (size_t i = 1; i < store->capacity; i++)
    {
        trap_action_t* trap = &store->traps[i];
        if (trap->is_ignored)
            continue;
        bool had_handler = trap->action != NULL;
        if (trap->action)
            string_destroy(&trap->action);
        trap->action = NULL;
        trap->is_default = true;
        trap->has_saved = false; /* a subshell starts from the defaults */
        if (had_handler)
            signal((int)i, SIG_DFL);
    }
    trap_store_clear_exit(store);
}

void trap_store_for_each_set_trap(const trap_store_t *store,
                                   void (*callback)(int signal_number,
                                                    const trap_action_t *trap,
                                                    void *context),
                                   void *context)
{
    Expects_not_null(store);
    Expects_not_null(callback);

    // Iterate through all signal traps in the store
    for (size_t i = 0; i < store->capacity; i++)
    {
        const trap_action_t *trap = &store->traps[i];

        // Only report traps that are actually set (not in default state)
        if (trap->is_default && !trap->is_ignored && !trap->action)
            continue; // This trap is not set, skip it

        // Call the callback for this trap
        callback((int)i, trap, context);
    }

#ifdef TRAP_USE_UCRT_SIGNALS
    // Also report FPE trap if it's set
    const trap_action_t *fpe_trap = &store->fpe_trap;
    if (!fpe_trap->is_default || fpe_trap->is_ignored || fpe_trap->action)
    {
        // FPE is a special case with its own trap, but we need to report it
        // Use SIGFPE as the signal number for the callback
        callback(SIGFPE, fpe_trap, context);
    }
#endif

    // Report EXIT trap if it's set (signal 0 represents EXIT)
    if (store->exit_trap_set && store->exit_action)
    {
        // Create a temporary trap_action_t for the EXIT trap
        trap_action_t exit_trap;
        exit_trap.signal_number = 0;
        exit_trap.action = store->exit_action;
        exit_trap.is_ignored = false;
        exit_trap.is_default = false;

        callback(0, &exit_trap, context);
    }
}

string_t *trap_store_take_exit_action(trap_store_t *store)
{
    Expects_not_null(store);
    if (!store->exit_trap_set)
        return NULL;
    string_t *action = store->exit_action;
    store->exit_action = NULL;
    store->exit_trap_set = false;
    if (action && string_length(action) == 0)
    {
        string_destroy(&action);
        return NULL;
    }
    return action;
}

void trap_store_record_ignored_on_entry(trap_store_t *store)
{
    Expects_not_null(store);
#ifdef TRAP_USE_POSIX_SIGNALS
    for (size_t i = 1; i < store->capacity; i++)
    {
        struct sigaction sa;
        if (sigaction((int)i, NULL, &sa) != 0)
            continue;
        if (sa.sa_handler == SIG_IGN)
        {
            trap_action_t *trap = &store->traps[i];
            trap->signal_number = (int)i;
            trap->is_ignored = true;
            trap->is_default = false;
            trap->ignored_on_entry = true;
        }
    }
#endif
}
