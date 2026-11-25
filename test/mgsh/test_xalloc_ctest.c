#include <string.h>
#include <setjmp.h>
#include "ctest.h"
#include "miga/xalloc.h"

// Helper macros to handle MIGA_ARENA_DEBUG and non-MIGA_ARENA_DEBUG modes
#ifdef MIGA_ARENA_DEBUG
#define TEST_ARENA_XMALLOC(arena, size) arena_xmalloc((arena), (size), __FILE__, __LINE__)
#define TEST_ARENA_XCALLOC(arena, n, size) arena_xcalloc((arena), (n), (size), __FILE__, __LINE__)
#define TEST_ARENA_XREALLOC(arena, ptr, size) arena_xrealloc((arena), (ptr), (size), __FILE__, __LINE__)
#define TEST_ARENA_XSTRDUP(arena, s) arena_xstrdup((arena), (s), __FILE__, __LINE__)
#define TEST_ARENA_XFREE(arena, ptr) arena_xfree((arena), (ptr), __FILE__, __LINE__)
#else
#define TEST_ARENA_XMALLOC(arena, size) arena_xmalloc((arena), (size))
#define TEST_ARENA_XCALLOC(arena, n, size) arena_xcalloc((arena), (n), (size))
#define TEST_ARENA_XREALLOC(arena, ptr, size) arena_xrealloc((arena), (ptr), (size))
#define TEST_ARENA_XSTRDUP(arena, s) arena_xstrdup((arena), (s))
#define TEST_ARENA_XFREE(arena, ptr) arena_xfree((arena), (ptr))
#endif

// Test arena_xmalloc basic allocation
CTEST(test_arena_xmalloc_basic)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    // Set up jump point for error handling
    if (setjmp(arena.rollback_point) == 0)
    {
        void *ptr = TEST_ARENA_XMALLOC(&arena, 100);
        CTEST_ASSERT_NOT_NULL(ctest, ptr, "allocated pointer should not be NULL");

        TEST_ARENA_XFREE(&arena, ptr);
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena_xcalloc zero initialization
CTEST(test_arena_xcalloc_zero_init)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        int *arr = TEST_ARENA_XCALLOC(&arena, 10, sizeof(int));
        CTEST_ASSERT_NOT_NULL(ctest, arr, "allocated array should not be NULL");

        // Verify zero initialization
        for (int i = 0; i < 10; i++)
        {
            CTEST_ASSERT_EQ(ctest, arr[i], 0, "array element should be zero");
        }

        TEST_ARENA_XFREE(&arena, arr);
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena_xrealloc resizing
CTEST(test_arena_xrealloc_resize)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        int *ptr = TEST_ARENA_XMALLOC(&arena, sizeof(int) * 5);
        CTEST_ASSERT_NOT_NULL(ctest, ptr, "initial allocation should not be NULL");

        // Set some values
        for (int i = 0; i < 5; i++)
        {
            ptr[i] = i * 10;
        }

        // Reallocate to larger size
        int *new_ptr = TEST_ARENA_XREALLOC(&arena, ptr, sizeof(int) * 10);
        CTEST_ASSERT_NOT_NULL(ctest, new_ptr, "reallocated pointer should not be NULL");

        // Verify original values are preserved
        for (int i = 0; i < 5; i++)
        {
            CTEST_ASSERT_EQ(ctest, new_ptr[i], i * 10, "original values should be preserved");
        }

        TEST_ARENA_XFREE(&arena, new_ptr);
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena_xstrdup string duplication
CTEST(test_arena_xstrdup_duplicate)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        const char *original = "Hello, World!";
        char *dup = TEST_ARENA_XSTRDUP(&arena, original);

        CTEST_ASSERT_NOT_NULL(ctest, dup, "duplicated string should not be NULL");
        CTEST_ASSERT_STR_EQ(ctest, dup, original, "duplicated string should match original");
        CTEST_ASSERT_TRUE(ctest, dup != original, "duplicated string should be different pointer");

        TEST_ARENA_XFREE(&arena, dup);
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Test TEST_ARENA_XFREE and basic tracking
CTEST(test_arena_xfree_tracking)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        void *ptr1 = TEST_ARENA_XMALLOC(&arena, 50);
        void *ptr2 = TEST_ARENA_XMALLOC(&arena, 100);
        void *ptr3 = TEST_ARENA_XMALLOC(&arena, 150);

        CTEST_ASSERT_NOT_NULL(ctest, ptr1, "ptr1 should be allocated");
        CTEST_ASSERT_NOT_NULL(ctest, ptr2, "ptr2 should be allocated");
        CTEST_ASSERT_NOT_NULL(ctest, ptr3, "ptr3 should be allocated");

        // Free in different order
        TEST_ARENA_XFREE(&arena, ptr2);
        TEST_ARENA_XFREE(&arena, ptr1);
        TEST_ARENA_XFREE(&arena, ptr3);
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Test arena lifecycle (init/reset/end)
CTEST(test_arena_lifecycle)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        // Allocate some memory
        void *ptr1 = TEST_ARENA_XMALLOC(&arena, 100);
        void *ptr2 = TEST_ARENA_XMALLOC(&arena, 200);

        CTEST_ASSERT_NOT_NULL(ctest, ptr1, "ptr1 should be allocated");
        CTEST_ASSERT_NOT_NULL(ctest, ptr2, "ptr2 should be allocated");

        // Reset should free all allocations
        arena_reset_ex(&arena);

        // After reset, we should be able to allocate again
        arena_init_ex(&arena);
        if (setjmp(arena.rollback_point) == 0)
        {
            void *ptr3 = TEST_ARENA_XMALLOC(&arena, 50);
            CTEST_ASSERT_NOT_NULL(ctest, ptr3, "ptr3 should be allocated after reset");
            TEST_ARENA_XFREE(&arena, ptr3);
        }
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Test multiple allocations and frees
CTEST(test_arena_multiple_allocs)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
#if __STDC_VERSION__ >= 202311L && !defined(_MSC_VER)
        constexpr int COUNT = 20;
#else
        #define COUNT 20
#endif
        void *ptrs[COUNT];

        // Allocate multiple blocks
        for (int i = 0; i < COUNT; i++)
        {
            ptrs[i] = TEST_ARENA_XMALLOC(&arena, (i + 1) * 10);
            CTEST_ASSERT_NOT_NULL(ctest, ptrs[i], "allocation should succeed");
        }

        // Free every other one
        for (int i = 0; i < COUNT; i += 2)
        {
            TEST_ARENA_XFREE(&arena, ptrs[i]);
        }

        // Free the rest
        for (int i = 1; i < COUNT; i += 2)
        {
            TEST_ARENA_XFREE(&arena, ptrs[i]);
        }
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Test that NULL can be safely passed to TEST_ARENA_XFREE
CTEST(test_arena_xfree_null)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        // This should not crash
        TEST_ARENA_XFREE(&arena, NULL);
        CTEST_ASSERT_TRUE(ctest, true, "freeing NULL should be safe");
    }

    arena_end_ex(&arena);
    (void)ctest;
}

#ifdef MIGA_ARENA_STATS
// Live, peak and cumulative counters follow allocations, reallocations and frees
CTEST(test_arena_stats_counters)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        miga_arena_stats_t st;
        void *a = TEST_ARENA_XMALLOC(&arena, 100);
        int *b = TEST_ARENA_XCALLOC(&arena, 10, sizeof(int));
        char *c = TEST_ARENA_XSTRDUP(&arena, "hello");

        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.live_blocks, 3, "three live blocks");
        CTEST_ASSERT_EQ(ctest, st.live_bytes, 100 + 10 * sizeof(int) + 6, "live bytes");
        CTEST_ASSERT_EQ(ctest, st.total_allocs, 3, "three allocations");

        a = TEST_ARENA_XREALLOC(&arena, a, 300);
        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.live_blocks, 3, "realloc keeps the block count");
        CTEST_ASSERT_EQ(ctest, st.live_bytes, 300 + 10 * sizeof(int) + 6, "realloc updates bytes");
        CTEST_ASSERT_EQ(ctest, st.total_reallocs, 1, "one reallocation");
        CTEST_ASSERT_EQ(ctest, st.largest_block, 300, "largest block");

        TEST_ARENA_XFREE(&arena, a);
        TEST_ARENA_XFREE(&arena, b);
        TEST_ARENA_XFREE(&arena, c);
        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.live_blocks, 0, "no live blocks after freeing");
        CTEST_ASSERT_EQ(ctest, st.live_bytes, 0, "no live bytes after freeing");
        CTEST_ASSERT_EQ(ctest, st.total_frees, 3, "three frees");
        CTEST_ASSERT_EQ(ctest, st.peak_blocks, 3, "peak blocks");
        CTEST_ASSERT_EQ(ctest, st.peak_bytes, 300 + 10 * sizeof(int) + 6, "peak bytes");
        CTEST_ASSERT_EQ(ctest, st.total_bytes_allocated, 100 + 10 * sizeof(int) + 6 + 300,
                        "total bytes allocated");
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// miga_arena_stats_reset_peak() starts a new high-water mark from the live state
CTEST(test_arena_stats_reset_peak)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        miga_arena_stats_t st;
        void *keep = TEST_ARENA_XMALLOC(&arena, 10);
        void *big = TEST_ARENA_XMALLOC(&arena, 1000);
        TEST_ARENA_XFREE(&arena, big);

        arena_stats_reset_peak_ex(&arena);
        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.peak_blocks, 1, "peak blocks reset to live");
        CTEST_ASSERT_EQ(ctest, st.peak_bytes, 10, "peak bytes reset to live");

        void *mid = TEST_ARENA_XMALLOC(&arena, 50);
        TEST_ARENA_XFREE(&arena, mid);
        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.peak_blocks, 2, "new peak blocks");
        CTEST_ASSERT_EQ(ctest, st.peak_bytes, 60, "new peak bytes");

        TEST_ARENA_XFREE(&arena, keep);
    }

    arena_end_ex(&arena);
    (void)ctest;
}

// Blocks that nobody freed before a reset show up as teardown (leaked) blocks
CTEST(test_arena_stats_teardown_leaks)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        miga_arena_stats_t st;
        (void)TEST_ARENA_XMALLOC(&arena, 24);
        (void)TEST_ARENA_XMALLOC(&arena, 40);
        void *freed = TEST_ARENA_XMALLOC(&arena, 8);
        TEST_ARENA_XFREE(&arena, freed);

        arena_reset_ex(&arena);
        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.teardown_blocks, 2, "two blocks leaked");
        CTEST_ASSERT_EQ(ctest, st.teardown_bytes, 64, "64 bytes leaked");
        CTEST_ASSERT_EQ(ctest, st.resets, 1, "one reset");
        CTEST_ASSERT_EQ(ctest, st.live_blocks, 0, "reset reclaims live blocks");
        CTEST_ASSERT_EQ(ctest, st.live_bytes, 0, "reset reclaims live bytes");
    }

    arena_end_ex(&arena);
    (void)ctest;
}

static void sum_live_block(const void *ptr, size_t size, void *user_data)
{
    size_t *totals = (size_t *)user_data;
    (void)ptr;
    totals[0]++;
    totals[1] += size;
}

// foreach_live visits every live block with its size; size classes bucket sizes
CTEST(test_arena_stats_foreach_live_and_size_classes)
{
    miga_arena_t arena = {0};
    arena_init_ex(&arena);

    if (setjmp(arena.rollback_point) == 0)
    {
        miga_arena_stats_t st;
        void *a = TEST_ARENA_XMALLOC(&arena, 16);   /* class 0: 1..16 */
        void *b = TEST_ARENA_XMALLOC(&arena, 17);   /* class 1: 17..32 */
        void *c = TEST_ARENA_XMALLOC(&arena, 1024); /* class 6: 513..1024 */

        size_t totals[2] = {0, 0};
        arena_stats_foreach_live_ex(&arena, sum_live_block, totals);
        CTEST_ASSERT_EQ(ctest, totals[0], 3, "foreach visits three blocks");
        CTEST_ASSERT_EQ(ctest, totals[1], 16 + 17 + 1024, "foreach reports block sizes");

        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.size_classes[0], 1, "one block of <=16 bytes");
        CTEST_ASSERT_EQ(ctest, st.size_classes[1], 1, "one block of 17..32 bytes");
        CTEST_ASSERT_EQ(ctest, st.size_classes[6], 1, "one block of 513..1024 bytes");

        arena_stats_clear_ex(&arena);
        arena_stats_get_ex(&arena, &st);
        CTEST_ASSERT_EQ(ctest, st.total_allocs, 0, "clear zeroes the counters");
        CTEST_ASSERT_EQ(ctest, st.size_classes[0], 0, "clear zeroes the size classes");
        CTEST_ASSERT_EQ(ctest, st.live_blocks, 3, "clear keeps the live blocks");
        CTEST_ASSERT_EQ(ctest, st.peak_blocks, 3, "clear sets the peak to live");

        TEST_ARENA_XFREE(&arena, a);
        TEST_ARENA_XFREE(&arena, b);
        TEST_ARENA_XFREE(&arena, c);
    }

    arena_end_ex(&arena);
    (void)ctest;
}
#endif /* MIGA_ARENA_STATS */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    CTestEntry *suite[] = {
        CTEST_ENTRY(test_arena_xmalloc_basic),
        CTEST_ENTRY(test_arena_xcalloc_zero_init),
        CTEST_ENTRY(test_arena_xrealloc_resize),
        CTEST_ENTRY(test_arena_xstrdup_duplicate),
        CTEST_ENTRY(test_arena_xfree_tracking),
        CTEST_ENTRY(test_arena_lifecycle),
        CTEST_ENTRY(test_arena_multiple_allocs),
        CTEST_ENTRY(test_arena_xfree_null),
#ifdef MIGA_ARENA_STATS
        CTEST_ENTRY(test_arena_stats_counters),
        CTEST_ENTRY(test_arena_stats_reset_peak),
        CTEST_ENTRY(test_arena_stats_teardown_leaks),
        CTEST_ENTRY(test_arena_stats_foreach_live_and_size_classes),
#endif
        NULL
    };

    int result = ctest_run_suite(suite);

    return result;
}
