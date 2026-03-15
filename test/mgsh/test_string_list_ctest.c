/*
 * test_strlist_ctest.c
 *
 * Unit tests for strlist.c - dynamic array of string_t pointers
 */

#include "ctest.h"
#include "miga/strlist.h"
#include "miga/string_t.h"
#include "lib.h"
#include "xalloc.h"

// ============================================================================
// Basic lifecycle
// ============================================================================

CTEST(test_strlist_create_destroy)
{
    strlist_t *list = strlist_create();
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 0, strlist_size(list), "size is 0");
    strlist_destroy(&list);
    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
}

CTEST(test_strlist_create_from_cstr_array_fixed_len)
{
    const char *strs[] = {"hello", "world", "test"};
    strlist_t *list = strlist_create_from_cstr_array(strs, 3);
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 3, strlist_size(list), "size is 3");

    const string_t *s0 = strlist_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s0), "hello", "element 0 is hello");

    const string_t *s2 = strlist_at(list, 2);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s2), "test", "element 2 is test");

    strlist_destroy(&list);
}

CTEST(test_strlist_create_from_cstr_array_null_terminated)
{
    const char *strs[] = {"one", "two", "three", NULL};
    strlist_t *list = strlist_create_from_cstr_array(strs, -1);
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 3, strlist_size(list), "size is 3");

    const string_t *s1 = strlist_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(s1), "two", "element 1 is two");

    strlist_destroy(&list);
}

CTEST(test_strlist_create_from_empty_array)
{
    const char *strs[] = {NULL};
    strlist_t *list = strlist_create_from_cstr_array(strs, 0);
    CTEST_ASSERT_NOT_NULL(ctest, list, "list created");
    CTEST_ASSERT_EQ(ctest, 0, strlist_size(list), "size is 0");
    strlist_destroy(&list);
}

// ============================================================================
// Element access
// ============================================================================

CTEST(test_strlist_at_valid_index)
{
    strlist_t *list = strlist_create();
    string_t *s1 = string_create_from_cstr("first");
    string_t *s2 = string_create_from_cstr("second");
    strlist_push_back(list, s1);
    strlist_push_back(list, s2);
    string_destroy(&s1);
    string_destroy(&s2);

    const string_t *elem = strlist_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "first", "at(0) is first");

    elem = strlist_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "second", "at(1) is second");

    strlist_destroy(&list);
}

CTEST(test_strlist_at_out_of_bounds)
{
    strlist_t *list = strlist_create();
    string_t *s = string_create_from_cstr("test");
    strlist_push_back(list, s);
    string_destroy(&s);

    const string_t *elem = strlist_at(list, 10);
    CTEST_ASSERT_NULL(ctest, elem, "at(10) returns NULL");

    elem = strlist_at(list, -1);
    CTEST_ASSERT_NULL(ctest, elem, "at(-1) returns NULL");

    strlist_destroy(&list);
}

// ============================================================================
// Push back operations
// ============================================================================

CTEST(test_strlist_push_back_copy)
{
    strlist_t *list = strlist_create();

    string_t *s1 = string_create_from_cstr("apple");
    strlist_push_back(list, s1);
    string_destroy(&s1);

    string_t *s2 = string_create_from_cstr("banana");
    strlist_push_back(list, s2);
    string_destroy(&s2);

    CTEST_ASSERT_EQ(ctest, 2, strlist_size(list), "size is 2");

    const string_t *elem0 = strlist_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem0), "apple", "elem 0 is apple");

    const string_t *elem1 = strlist_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "banana", "elem 1 is banana");

    strlist_destroy(&list);
}

CTEST(test_strlist_move_push_back)
{
    strlist_t *list = strlist_create();

    string_t *s1 = string_create_from_cstr("moved");
    strlist_move_push_back(list, &s1);
    CTEST_ASSERT_NULL(ctest, s1, "source set to NULL after move");

    CTEST_ASSERT_EQ(ctest, 1, strlist_size(list), "size is 1");
    const string_t *elem = strlist_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "moved", "elem is moved");

    strlist_destroy(&list);
}

CTEST(test_strlist_growth)
{
    strlist_t *list = strlist_create();

    // Add more than initial capacity (4)
    for (int i = 0; i < 10; i++)
    {
        char buf[20];
        snprintf(buf, sizeof(buf), "item%d", i);
        string_t *s = string_create_from_cstr(buf);
        strlist_push_back(list, s);
        string_destroy(&s);
    }

    CTEST_ASSERT_EQ(ctest, 10, strlist_size(list), "size is 10");
    CTEST_ASSERT_GT(ctest, (int)list->capacity, 4, "capacity grew");

    const string_t *elem = strlist_at(list, 9);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "item9", "last elem correct");

    strlist_destroy(&list);
}

// ============================================================================
// Insert operations
// ============================================================================

CTEST(test_strlist_insert_at_beginning)
{
    strlist_t *list = strlist_create();

    string_t *s1 = string_create_from_cstr("second");
    strlist_push_back(list, s1);
    string_destroy(&s1);

    string_t *s2 = string_create_from_cstr("first");
    strlist_insert(list, 0, s2);
    string_destroy(&s2);

    CTEST_ASSERT_EQ(ctest, 2, strlist_size(list), "size is 2");

    const string_t *elem0 = strlist_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem0), "first", "elem 0 is first");

    const string_t *elem1 = strlist_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "second", "elem 1 is second");

    strlist_destroy(&list);
}

CTEST(test_strlist_insert_at_end)
{
    strlist_t *list = strlist_create();

    string_t *s1 = string_create_from_cstr("first");
    strlist_push_back(list, s1);
    string_destroy(&s1);

    string_t *s2 = string_create_from_cstr("second");
    strlist_insert(list, 1, s2);
    string_destroy(&s2);

    CTEST_ASSERT_EQ(ctest, 2, strlist_size(list), "size is 2");

    const string_t *elem1 = strlist_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "second", "elem 1 is second");

    strlist_destroy(&list);
}

CTEST(test_strlist_insert_clamps_negative_index)
{
    strlist_t *list = strlist_create();

    string_t *s1 = string_create_from_cstr("elem");
    strlist_insert(list, -5, s1);
    string_destroy(&s1);

    CTEST_ASSERT_EQ(ctest, 1, strlist_size(list), "size is 1");
    const string_t *elem = strlist_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem), "elem", "inserted at start");

    strlist_destroy(&list);
}

CTEST(test_strlist_insert_null_creates_empty_string)
{
    strlist_t *list = strlist_create();

    strlist_insert(list, 0, NULL);

    CTEST_ASSERT_EQ(ctest, 1, strlist_size(list), "size is 1");
    const string_t *elem = strlist_at(list, 0);
    CTEST_ASSERT_EQ(ctest, 0, (int)string_length(elem), "empty string");

    strlist_destroy(&list);
}

CTEST(test_strlist_move_insert)
{
    strlist_t *list = strlist_create();

    string_t *s1 = string_create_from_cstr("first");
    string_t *s2 = string_create_from_cstr("second");
    strlist_move_push_back(list, &s1);
    strlist_move_push_back(list, &s2);

    string_t *mid = string_create_from_cstr("middle");
    strlist_move_insert(list, 1, &mid);
    CTEST_ASSERT_NULL(ctest, mid, "source set to NULL");

    CTEST_ASSERT_EQ(ctest, 3, strlist_size(list), "size is 3");

    const string_t *elem1 = strlist_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "middle", "elem 1 is middle");

    strlist_destroy(&list);
}

// ============================================================================
// Erase and clear
// ============================================================================

CTEST(test_strlist_erase_middle)
{
    strlist_t *list = strlist_create();

    const char *strs[] = {"a", "b", "c"};
    for (int i = 0; i < 3; i++)
    {
        string_t *s = string_create_from_cstr(strs[i]);
        strlist_push_back(list, s);
        string_destroy(&s);
    }

    strlist_erase(list, 1);

    CTEST_ASSERT_EQ(ctest, 2, strlist_size(list), "size is 2");

    const string_t *elem0 = strlist_at(list, 0);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem0), "a", "elem 0 is a");

    const string_t *elem1 = strlist_at(list, 1);
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(elem1), "c", "elem 1 is c");

    strlist_destroy(&list);
}

CTEST(test_strlist_clear)
{
    strlist_t *list = strlist_create();

    for (int i = 0; i < 5; i++)
    {
        char buf[10];
        snprintf(buf, sizeof(buf), "item%d", i);
        string_t *s = string_create_from_cstr(buf);
        strlist_push_back(list, s);
        string_destroy(&s);
    }

    CTEST_ASSERT_EQ(ctest, 5, strlist_size(list), "size was 5");

    strlist_clear(list);

    CTEST_ASSERT_EQ(ctest, 0, strlist_size(list), "size is 0 after clear");

    strlist_destroy(&list);
}

// ============================================================================
// Conversion utilities
// ============================================================================

CTEST(test_strlist_release_cstr_array)
{
    strlist_t *list = strlist_create();

    string_t *s1 = string_create_from_cstr("foo");
    string_t *s2 = string_create_from_cstr("bar");
    strlist_push_back(list, s1);
    strlist_push_back(list, s2);
    string_destroy(&s1);
    string_destroy(&s2);

    int out_size = 0;
    char **arr = strlist_release_cstr_array(&list, &out_size);

    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
    CTEST_ASSERT_EQ(ctest, 2, out_size, "out_size is 2");
    CTEST_ASSERT_STR_EQ(ctest, arr[0], "foo", "arr[0] is foo");
    CTEST_ASSERT_STR_EQ(ctest, arr[1], "bar", "arr[1] is bar");
    CTEST_ASSERT_NULL(ctest, arr[2], "arr[2] is NULL");

    xfree(arr[0]);
    xfree(arr[1]);
    xfree(arr);
}

CTEST(test_strlist_join_move_with_separator)
{
    strlist_t *list = strlist_create();

    const char *strs[] = {"one", "two", "three"};
    for (int i = 0; i < 3; i++)
    {
        string_t *s = string_create_from_cstr(strs[i]);
        strlist_push_back(list, s);
        string_destroy(&s);
    }

    string_t *result = strlist_join_move(&list, " ");

    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(result), "one two three", "joined string correct");

    string_destroy(&result);
}

CTEST(test_strlist_join_move_empty_list)
{
    strlist_t *list = strlist_create();

    string_t *result = strlist_join_move(&list, ",");

    CTEST_ASSERT_NULL(ctest, list, "list destroyed");
    CTEST_ASSERT_EQ(ctest, 0, (int)string_length(result), "empty result");

    string_destroy(&result);
}

// ============================================================================
// Main test runner
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    miga_setjmp();

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_strlist_create_destroy),
        CTEST_ENTRY(test_strlist_create_from_cstr_array_fixed_len),
        CTEST_ENTRY(test_strlist_create_from_cstr_array_null_terminated),
        CTEST_ENTRY(test_strlist_create_from_empty_array),

        CTEST_ENTRY(test_strlist_at_valid_index),
        CTEST_ENTRY(test_strlist_at_out_of_bounds),

        CTEST_ENTRY(test_strlist_push_back_copy),
        CTEST_ENTRY(test_strlist_move_push_back),
        CTEST_ENTRY(test_strlist_growth),

        CTEST_ENTRY(test_strlist_insert_at_beginning),
        CTEST_ENTRY(test_strlist_insert_at_end),
        CTEST_ENTRY(test_strlist_insert_clamps_negative_index),
        CTEST_ENTRY(test_strlist_insert_null_creates_empty_string),
        CTEST_ENTRY(test_strlist_move_insert),

        CTEST_ENTRY(test_strlist_erase_middle),
        CTEST_ENTRY(test_strlist_clear),

        CTEST_ENTRY(test_strlist_release_cstr_array),
        CTEST_ENTRY(test_strlist_join_move_with_separator),
        CTEST_ENTRY(test_strlist_join_move_empty_list),

        NULL
    };

    int result = ctest_run_suite(suite);

    miga_arena_end();

    return result;
}
