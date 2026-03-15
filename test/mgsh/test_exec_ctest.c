#include <string.h>
#include "ctest.h"
#include "miga/exec.h"
#include "exec_frame.h"
#include "logging.h"
#include "miga/string_t.h"

#if 0

/* ============================================================================
 * Test: Executor Creation and Destruction
 * ============================================================================ */

CTEST(test_exec_create_destroy)
{
    exec_t *executor = exec_create();
	exec_set_flag_xtrace(executor, true);

    // Verify executor was created
    CTEST_ASSERT_NOT_NULL(ctest, executor, "executor should be created");

    // Verify basic fields are initialized
    CTEST_ASSERT_EQ(ctest, exec_get_last_exit_status(executor), 0, "last_exit_status should be 0");
    CTEST_ASSERT_NOT_NULL(ctest, exec_get_error(executor), "error_msg should be initialized");
    CTEST_ASSERT_NOT_NULL(ctest, frame_has_variable_cstr(exec_get_current_frame(executor), "SHELL"), "SHELL variable should be initialized");
    CTEST_ASSERT_NOT_NULL(ctest, frame_has_positional_params(exec_get_current_frame(executor)), "positional_params should be initialized");

    // Verify new special variable fields are initialized
    CTEST_ASSERT_EQ(ctest, exec_get_last_background_pid(executor), 0, "last_background_pid should be 0");
#ifdef MIGA_POSIX_API
    CTEST_ASSERT(ctest, exec_get_shell_pid(executor) > 0, "shell_pid should be set to getpid() on POSIX");
#else
    CTEST_ASSERT_EQ(ctest, exec_get_shell_pid(executor), 0, "shell_pid should be 0 on non-POSIX");
#endif
    CTEST_ASSERT_NOT_NULL(ctest, exec_get_last_argument(executor), "last_argument should be initialized");
    CTEST_ASSERT_EQ(ctest, string_length(exec_get_last_argument(executor)), 0, "last_argument should be empty");
    // CTEST_ASSERT_EQ(ctest, executor->opt_flags_set, true, "shell_flags should be initialized");

    // Clean up
    exec_destroy(&executor);
    CTEST_ASSERT_NULL(ctest, executor, "executor should be NULL after destroy");

    (void)ctest;
}

/* ============================================================================
 * Test: Executor Special Variables
 * ============================================================================ */

CTEST(test_exec_special_variables)
{
    exec_cfg_t cfg = {.opt.xtrace = true};
    exec_t *executor = exec_create(&cfg);

    // Test that we can modify special variable fields
    executor->last_background_pid = 12345;
    CTEST_ASSERT_EQ(ctest, executor->last_background_pid, 12345, "last_background_pid should be settable");

    // Test string fields
    string_append_cstr(executor->last_argument, "test_arg");
    CTEST_ASSERT_EQ(ctest, string_length(executor->last_argument), 8, "last_argument should have length 8");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(executor->last_argument), "test_arg", "last_argument should contain 'test_arg'");

    CTEST_ASSERT_EQ(ctest, executor->opt.xtrace, true, "xtrace option should be true");

    // Clean up (this verifies that string_destroy is called properly)
    exec_destroy(&executor);
    CTEST_ASSERT_NULL(ctest, executor, "executor should be NULL after destroy");

    (void)ctest;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void)
{
    // Set log level to suppress debug output during tests
    log_set_level(LOG_LEVEL_ERROR);

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_exec_create_destroy),
        CTEST_ENTRY(test_exec_special_variables),
        NULL
    };

    int result = ctest_run_suite(suite);

    return result;
}

#else
int main()
{
    return 0;
}
#endif