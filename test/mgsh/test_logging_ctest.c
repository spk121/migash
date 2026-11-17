/*
 * log_init(): the developer logging configuration from the environment
 * (docs/logging.md).
 */
#include <stdlib.h>

#include "ctest.h"
#include "logging.h"
#include "miga/xalloc.h"

static void set_env(const char *name, const char *value)
{
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
}

CTEST(test_default_level_without_environment)
{
    set_env("MIGASH_LOG_LEVEL", NULL);
    set_env("MIGASH_LOG_ABORT_LEVEL", NULL);
    log_init();
    CTEST_ASSERT_EQ(ctest, log_get_level(), log_default_level(), "unset: the build's default level");
    CTEST_ASSERT_EQ(ctest, log_get_abort_level(), LOG_LEVEL_FATAL, "unset: abort at fatal");
#ifdef MIGA_DEBUG_BUILD
    CTEST_ASSERT_EQ(ctest, log_default_level(), LOG_LEVEL_WARN, "debug build: warn by default");
#else
    CTEST_ASSERT_EQ(ctest, log_default_level(), LOG_LEVEL_NONE, "release build: silent by default");
#endif
    (void)ctest;
}

CTEST(test_level_names)
{
    const char *names[] = {"debug", "WARN", "Error", "fatal", "NONE"};
    LogLevel levels[] = {LOG_LEVEL_DEBUG, LOG_LEVEL_WARN, LOG_LEVEL_ERROR, LOG_LEVEL_FATAL, LOG_LEVEL_NONE};
    for (int i = 0; i < 5; i++)
    {
        set_env("MIGASH_LOG_LEVEL", names[i]);
        log_init();
        CTEST_ASSERT_EQ(ctest, log_get_level(), levels[i], "MIGASH_LOG_LEVEL names, any case");
    }
    set_env("MIGASH_LOG_LEVEL", NULL);
    (void)ctest;
}

CTEST(test_unknown_level_keeps_default)
{
    set_env("MIGASH_LOG_LEVEL", "verbose");
    log_init();
    CTEST_ASSERT_EQ(ctest, log_get_level(), log_default_level(), "an unknown name leaves the default");
    set_env("MIGASH_LOG_LEVEL", "");
    log_init();
    CTEST_ASSERT_EQ(ctest, log_get_level(), log_default_level(), "an empty value leaves the default");
    set_env("MIGASH_LOG_LEVEL", NULL);
    (void)ctest;
}

CTEST(test_old_variable_is_ignored)
{
    set_env("LOG_LEVEL", "debug");
    set_env("MIGASH_LOG_LEVEL", NULL);
    log_init();
    CTEST_ASSERT_EQ(ctest, log_get_level(), log_default_level(), "LOG_LEVEL belongs to other programs");
    set_env("LOG_LEVEL", NULL);
    (void)ctest;
}

CTEST(test_abort_level)
{
    set_env("MIGASH_LOG_ABORT_LEVEL", "none");
    log_init();
    CTEST_ASSERT_EQ(ctest, log_get_abort_level(), LOG_LEVEL_NONE, "abort level none");
    set_env("MIGASH_LOG_ABORT_LEVEL", "error");
    log_init();
    CTEST_ASSERT_EQ(ctest, log_get_abort_level(), LOG_LEVEL_ERROR, "abort level error");
    set_env("MIGASH_LOG_ABORT_LEVEL", "debug");
    log_init();
    CTEST_ASSERT_EQ(ctest, log_get_abort_level(), LOG_LEVEL_FATAL, "debug is not an abort level");
    set_env("MIGASH_LOG_ABORT_LEVEL", NULL);
    log_init();
    (void)ctest;
}

CTEST(test_set_level_overrides)
{
    log_init();
    log_set_level(LOG_LEVEL_DEBUG);
    CTEST_ASSERT_EQ(ctest, log_get_level(), LOG_LEVEL_DEBUG, "log_set_level");
    log_set_level(LOG_LEVEL_NONE);
    CTEST_ASSERT_EQ(ctest, log_get_level(), LOG_LEVEL_NONE, "log_set_level none");
    (void)ctest;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    miga_setjmp();

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_default_level_without_environment),
        CTEST_ENTRY(test_level_names),
        CTEST_ENTRY(test_unknown_level_keeps_default),
        CTEST_ENTRY(test_old_variable_is_ignored),
        CTEST_ENTRY(test_abort_level),
        CTEST_ENTRY(test_set_level_overrides),
        NULL
    };

    int result = ctest_run_suite(suite);

    miga_arena_end();

    return result;
}
