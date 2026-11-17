#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#include "logging.h"

#ifdef MIGA_POSIX_API
#include <strings.h> // For strcasecmp
#elifdef MIGA_UCRT_API
#include <string.h> // For _stricmp
#else
#include "lib.h" // For ascii_strcasecmp
#endif

// Constants
#define LOG_MESSAGE_BUFFER_SIZE 1024

// The threshold: developer output is off in a release build; a debug build
// shows warnings and errors (docs/logging.md).
#ifdef MIGA_DEBUG_BUILD
#define LOG_DEFAULT_LEVEL LOG_LEVEL_WARN
#else
#define LOG_DEFAULT_LEVEL LOG_LEVEL_NONE
#endif
LogLevel g_log_threshold = LOG_DEFAULT_LEVEL;

// Define the abort level
static LogLevel g_log_abort_level = LOG_LEVEL_FATAL; // Default to no aborting, except for FATAL

// Fatal try/catch support
static jmp_buf *g_fatal_jmp = NULL;
static int g_fatal_jmp_enabled = 0;
static LogLevel g_prev_abort_level = LOG_LEVEL_FATAL;
static int g_prev_abort_level_valid = 0;

// Internal logging function
static void log_message(LogLevel level, const char *level_str, const char *format, va_list args)
{
    /* A fatal message is the last word before abort(): always printed. */
    if (level < g_log_threshold && level != LOG_LEVEL_FATAL)
    {
        if (level >= g_log_abort_level && level != LOG_LEVEL_NONE)
        {
            abort();
        }
        if (level == LOG_LEVEL_FATAL && g_log_abort_level == LOG_LEVEL_NONE)
        {
            if (g_fatal_jmp_enabled && g_fatal_jmp != NULL)
            {
                longjmp(*g_fatal_jmp, 1);
            }
        }
        return;
    }

    char *buffer = malloc(LOG_MESSAGE_BUFFER_SIZE);
    if (buffer == NULL)
    {
        fprintf(stderr, "[%s] Failed to allocate log buffer\n", level_str);
        fflush(stderr);
        if (level >= g_log_abort_level && level != LOG_LEVEL_NONE)
        {
            abort();
        }
        if (level == LOG_LEVEL_FATAL && g_log_abort_level == LOG_LEVEL_NONE)
        {
            if (g_fatal_jmp_enabled && g_fatal_jmp != NULL)
            {
                longjmp(*g_fatal_jmp, 1);
            }
        }
        return;
    }

    if (!(level == LOG_LEVEL_FATAL && g_fatal_jmp_enabled))
    {
        int written = vsnprintf(buffer, LOG_MESSAGE_BUFFER_SIZE, format, args);
        if (written < 0)
        {
            fprintf(stderr, "[%s] Failed to format log message\n", level_str);
        }
        else if (written >= LOG_MESSAGE_BUFFER_SIZE)
        {
            fprintf(stderr, "[%s] Log message truncated (needed %d bytes)\n", level_str, written + 1);
            fprintf(stderr, "[%s] %s\n", level_str, buffer);
        }
        else
        {
            fprintf(stderr, "[%s] %s\n", level_str, buffer);
        }
        fflush(stderr);
    }
    free(buffer);

    // Check if this level should trigger abort
    if (level >= g_log_abort_level && level != LOG_LEVEL_NONE)
    {
        abort();
    }

    if (level == LOG_LEVEL_FATAL && g_log_abort_level == LOG_LEVEL_NONE)
    {
        if (g_fatal_jmp_enabled && g_fatal_jmp != NULL)
        {
            longjmp(*g_fatal_jmp, 1);
        }
    }
}

static int strcompare(const char *s1, const char *s2)
{
#ifdef MIGA_POSIX_API
    return strcasecmp(s1, s2);
#elifdef MIGA_UCRT_API
    return _stricmp(s1, s2);
#else
    // Fallback for MIGA_ISO_C: ASCII-only case-sensitive comparison
    return ascii_strcasecmp(s1, s2);
#endif
}

static bool parse_level(const char *text, LogLevel *out)
{
    static const struct
    {
        const char *name;
        LogLevel level;
    } names[] = {{"debug", LOG_LEVEL_DEBUG},
                 {"warn", LOG_LEVEL_WARN},
                 {"error", LOG_LEVEL_ERROR},
                 {"fatal", LOG_LEVEL_FATAL},
                 {"none", LOG_LEVEL_NONE}};
    for (size_t k = 0; k < sizeof names / sizeof names[0]; k++)
    {
        if (strcompare(text, names[k].name) == 0)
        {
            *out = names[k].level;
            return true;
        }
    }
    return false;
}

LogLevel log_default_level(void)
{
    return LOG_DEFAULT_LEVEL;
}

// Initialization from the environment (docs/logging.md)
void log_init(void)
{
    LogLevel lv;
    const char *env_level = getenv("MIGASH_LOG_LEVEL");
    g_log_threshold = LOG_DEFAULT_LEVEL;
    if (env_level != NULL && parse_level(env_level, &lv))
        g_log_threshold = lv;

    const char *env_abort_level = getenv("MIGASH_LOG_ABORT_LEVEL");
    g_log_abort_level = LOG_LEVEL_FATAL;
    if (env_abort_level != NULL && parse_level(env_abort_level, &lv) && lv != LOG_LEVEL_DEBUG)
        g_log_abort_level = lv;
}

LogLevel log_get_abort_level(void)
{
    return g_log_abort_level;
}

LogLevel log_level(void)
{
    return g_log_threshold;
}

void log_set_level(LogLevel lv)
{
    Expects_ge(lv, LOG_LEVEL_DEBUG);
    Expects_le(lv, LOG_LEVEL_NONE);

    g_log_threshold = lv;
}

LogLevel log_get_level(void)
{
    return g_log_threshold;
}

void log_disable_abort(void)
{
    g_log_abort_level = LOG_LEVEL_NONE;
}

void log_set_abort_level(LogLevel lv)
{
    Expects_ge(lv, LOG_LEVEL_DEBUG);
    Expects_le(lv, LOG_LEVEL_NONE);

    g_log_abort_level = lv;
}

void log_fatal_try_setup(jmp_buf *env)
{
    Expects_not_null(env);
    g_prev_abort_level = g_log_abort_level;
    g_prev_abort_level_valid = 1;
    g_log_abort_level = LOG_LEVEL_NONE;
    g_fatal_jmp = env;
    g_fatal_jmp_enabled = 1;
}

void log_fatal_try_end(void)
{
    g_fatal_jmp = NULL;
    g_fatal_jmp_enabled = 0;
    if (g_prev_abort_level_valid)
    {
        g_log_abort_level = g_prev_abort_level;
        g_prev_abort_level_valid = 0;
    }
}

// Public logging functions
void log_debug(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_DEBUG, "DEBUG", format, args);
    va_end(args);
}

void log_warn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_WARN, "WARN", format, args);
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_ERROR, "ERROR", format, args);
    va_end(args);
}

void log_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_message(LOG_LEVEL_FATAL, "FATAL", format, args);
    va_end(args);
}

#ifdef EXAMPLE_USAGE
int test_function(float fvalue, double dvalue, int ivalue)
{
    char c1 = 'a', c2 = 'a';
    short s1 = 5, s2 = 10;
    long l1 = 100L, l2 = 50L;

    return_if_eq(c1, c2);              // char comparison
    return_val_if_lt(s1, s2, -1);      // short comparison
    return_if_gt(fvalue, 0.0f);        // float comparison
    return_val_if_le(dvalue, 1.0, 42); // double comparison
    return_if_ge(l1, l2);              // long comparison
    return_val_if_eq(ivalue, 5, 99);   // int comparison

    // Example usage of Expects
    Expects_not_null(&ivalue);
    Expects(ivalue > 0);
    Expects_eq(ivalue, 5);

    return 0;
}

int main(void)
{
    log_init();

    test_function(1.5f, 0.5, 5);

    return 0;
}
#endif
