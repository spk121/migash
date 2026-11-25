/**
 * @file test_memory_leak_ctest.c
 * @brief Memory-leak regression tests for the executor
 *
 * The arena frees every block it tracks at teardown, so a leak never shows up
 * as lost memory: it shows up as live blocks that pile up while the shell
 * runs. These tests run a command string once to warm up (first runs may
 * legitimately create variables, functions or table entries), record the
 * arena's live allocations, run the same string again several times, and
 * require the live allocations to come back to the recorded value.
 *
 * Each case targets a construct that leaked in the past. A whole executor
 * lifecycle is also checked: creating, using and destroying an executor must
 * leave the arena where it started.
 *
 * With MIGA_ARENA_STATS (-DENABLE_ARENA_STATS=ON) live bytes are compared as
 * well as live blocks; otherwise the arena's allocation count is used, so the
 * tests run in every configuration.
 */

#include <stddef.h>
#include <stdio.h>

#include "ctest.h"
#include "logging.h"
#include "miga/exec.h"
#include "miga/xalloc.h"

/* Number of extra runs after the warm-up run */
#define LEAK_TEST_REPEATS 5

typedef struct
{
    size_t blocks;
    size_t bytes; /* 0 unless MIGA_ARENA_STATS */
} live_memory_t;

static live_memory_t live_memory(void)
{
    live_memory_t m;
#ifdef MIGA_ARENA_STATS
    miga_arena_stats_t st;
    miga_arena_stats_get(&st);
    m.blocks = st.live_blocks;
    m.bytes = st.live_bytes;
#else
    m.blocks = (size_t)miga_arena_get_global()->allocated_count;
    m.bytes = 0;
#endif
    return m;
}

static bool same_live_memory(live_memory_t a, live_memory_t b)
{
    return a.blocks == b.blocks && a.bytes == b.bytes;
}

static void report_growth(const char *what, live_memory_t before, live_memory_t after)
{
    fprintf(stderr, "# %s: live memory went from %zu blocks, %zu bytes to %zu blocks, %zu bytes\n",
            what, before.blocks, before.bytes, after.blocks, after.bytes);
}

/*
 * Run COMMAND once to warm up, then LEAK_TEST_REPEATS more times, and report
 * whether the live allocations came back to their value after the warm-up.
 */
static bool command_is_leak_free(const char *command)
{
    miga_exec_t *executor = exec_create();
    exec_setup_noninteractive(executor);

    (void)exec_execute_command_string(executor, command);
    live_memory_t before = live_memory();

    for (int i = 0; i < LEAK_TEST_REPEATS; i++)
        (void)exec_execute_command_string(executor, command);
    live_memory_t after = live_memory();

    exec_destroy(&executor);

    if (!same_live_memory(before, after))
    {
        report_growth(command, before, after);
        return false;
    }
    return true;
}

/*
 * Like command_is_leak_free(), but feed SCRIPT through exec_execute_stream(),
 * which reads and parses it line by line, as the shell does for a script file
 * (incomplete compound commands are carried over between lines). The script
 * goes through an ISO C tmpfile() so that this works in every configuration.
 * Returns false (after a diagnostic) if no temporary file can be created.
 */
static bool stream_is_leak_free(const char *script)
{
    FILE *fp = tmpfile();
    if (!fp)
    {
        fprintf(stderr, "# tmpfile() failed\n");
        return false;
    }
    fputs(script, fp);

    miga_exec_t *executor = exec_create();
    exec_setup_noninteractive(executor);

    rewind(fp);
    (void)exec_execute_stream(executor, fp);
    live_memory_t before = live_memory();

    for (int i = 0; i < LEAK_TEST_REPEATS; i++)
    {
        rewind(fp);
        (void)exec_execute_stream(executor, fp);
    }
    live_memory_t after = live_memory();

    exec_destroy(&executor);
    fclose(fp);

    if (!same_live_memory(before, after))
    {
        report_growth(script, before, after);
        return false;
    }
    return true;
}

#define ASSERT_NO_LEAK(ctest, command, msg)                                                        \
    CTEST_ASSERT_TRUE(ctest, command_is_leak_free(command), msg)

/* ============================================================================
 * Executor lifecycle
 * ============================================================================ */

/* exec_destroy() must free everything exec_create() and setup allocated
 * (it once leaked the builtin store, env_vars and $0). */
CTEST(test_leak_executor_lifecycle)
{
    live_memory_t before = live_memory();

    miga_exec_t *executor = exec_create();
    exec_setup_noninteractive(executor);
    (void)exec_execute_command_string(executor, "x=1; : \"$x\"; set --");
    exec_destroy(&executor);

    live_memory_t after = live_memory();
    if (!same_live_memory(before, after))
        report_growth("executor lifecycle", before, after);
    CTEST_ASSERT_TRUE(ctest, same_live_memory(before, after),
                      "create, run and destroy returns the arena to where it started");
}

/* ============================================================================
 * Word expansion
 * ============================================================================ */

/* Field splitting and expansion results (strlist_push_back temporaries,
 * the field builder's leftover string). */
CTEST(test_leak_word_expansion)
{
    ASSERT_NO_LEAK(ctest, "x='a b c'; : $x \"$x\" ${x%% *} ${x#a} $((1 + 2))",
                   "expanding and splitting words does not leak");
}

/* An unmatched glob pattern is kept as a literal word. */
CTEST(test_leak_unmatched_glob)
{
    ASSERT_NO_LEAK(ctest, ": no_such_file_*.xyz", "an unmatched pattern does not leak");
}

/* Loop bodies run many simple commands per execution. */
CTEST(test_leak_loop)
{
    ASSERT_NO_LEAK(ctest, "i=0; while [ $i -lt 10 ]; do i=$((i + 1)); : $i; done",
                   "a while loop does not leak");
}

/* ============================================================================
 * Parsing
 * ============================================================================ */

/* A command string that ends inside a compound command is rejected as
 * incomplete: the error path must free what was parsed so far. */
CTEST(test_leak_syntax_error)
{
    ASSERT_NO_LEAK(ctest, "if true; then :", "a syntax error does not leak");
}

/* A compound command spread over several lines of a script: while it is
 * incomplete the tokenizer records an error for each line read
 * (tokenizer_set_error() leaked the previous message). */
CTEST(test_leak_multiline_compound_command)
{
    CTEST_ASSERT_TRUE(ctest, stream_is_leak_free("if true\nthen\n  :\nfi\n"),
                      "reading a multi-line compound command from a stream does not leak");
}

/* Here-document delimiters were cloned and never freed. */
CTEST(test_leak_here_document)
{
    ASSERT_NO_LEAK(ctest, "read v <<EOF\nhello\nEOF\n: \"$v\"",
                   "a here-document does not leak");
}

/* ============================================================================
 * Redirections
 * ============================================================================ */

/* Duplicating a descriptor (the target text was not freed). */
CTEST(test_leak_fd_duplication)
{
    ASSERT_NO_LEAK(ctest, ": 2>&1", "duplicating a descriptor does not leak");
}

/* Closing a descriptor (the target token of '>&-' was not freed). */
CTEST(test_leak_fd_close)
{
    ASSERT_NO_LEAK(ctest, ": 3>&-", "closing a descriptor does not leak");
}

/* ============================================================================
 * Functions
 * ============================================================================ */

/* Defining and calling a function (the name node freed its token as a
 * string). */
CTEST(test_leak_function)
{
    ASSERT_NO_LEAK(ctest, "f() { : \"$1\"; }; f a", "defining and calling a function does not leak");
}

/* A function with redirections (the redirect list and its exec copy). */
CTEST(test_leak_function_with_redirections)
{
    ASSERT_NO_LEAK(ctest, "g() { :; } 2>&1; g",
                   "defining a function with redirections does not leak");
}

int main(void)
{
    log_set_level(LOG_LEVEL_ERROR);

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_leak_executor_lifecycle),
        CTEST_ENTRY(test_leak_word_expansion),
        CTEST_ENTRY(test_leak_unmatched_glob),
        CTEST_ENTRY(test_leak_loop),
        CTEST_ENTRY(test_leak_syntax_error),
        CTEST_ENTRY(test_leak_multiline_compound_command),
        CTEST_ENTRY(test_leak_here_document),
        CTEST_ENTRY(test_leak_fd_duplication),
        CTEST_ENTRY(test_leak_fd_close),
        CTEST_ENTRY(test_leak_function),
        CTEST_ENTRY(test_leak_function_with_redirections),
        NULL
    };

    return ctest_run_suite(suite);
}
