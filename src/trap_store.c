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

void trap_handler(int signo)
{
    note_pending(signo);
    /* ISO C does not say whether the disposition is reset: re-arm */
    signal(signo, trap_handler);
}

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
    signal(signal_number, trap->has_saved ? trap->saved_handler : SIG_DFL);
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


    // Signal number is not recognized in the available signal set
    // Check if it's a known signal on any platform but unsupported here
    // We need to check if it's a known ISO C signal that we couldn't match above
    // (This shouldn't happen as we check all ISO C signals)

    // Check for known POSIX signals that might not be available on this platform
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
}
