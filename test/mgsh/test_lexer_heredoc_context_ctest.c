/*
 * The lexer's here-document context stack: lexer_push_heredoc_context()
 * saves the pending here-documents (queue, index, reading flag) and starts
 * an empty context for a nested lex; lexer_pop_heredoc_context() restores
 * them. This is the ground for lexing a "$( ... )" command substitution
 * with the real lexer: here-documents inside the substitution must be read
 * inside it, while the enclosing line's own here-documents wait until the
 * substitution is finished.
 */
#include <string.h>

#include "ctest.h"

#define LEXER_INTERNAL
#include "lexer.h"
#include "lexer_heredoc.h"
#include "lexer_normal.h"
#include "token.h"
#include "miga/string_t.h"
#include "miga/xalloc.h"

static void queue(lexer_t *lx, const char *delim, bool strip_tabs, bool quoted)
{
    string_t *d = string_create_from_cstr(delim);
    lexer_queue_heredoc(lx, d, strip_tabs, quoted);
    string_destroy(&d);
}

static const token_t *find_end_of_heredoc(const token_list_t *tokens, const char *delim)
{
    for (int i = 0; i < token_list_size(tokens); i++)
    {
        const token_t *t = token_list_get(tokens, i);
        if (token_get_type(t) == TOKEN_END_OF_HEREDOC && t->heredoc_delimiter &&
            strcmp(string_cstr(t->heredoc_delimiter), delim) == 0)
            return t;
    }
    return NULL;
}

/* A fresh lexer is at the top level with nothing pending. */
CTEST(test_context_initial_state)
{
    lexer_t *lx = lexer_create();

    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 0, "depth starts at 0");
    CTEST_ASSERT_FALSE(ctest, lexer_heredoc_pending(lx), "nothing pending");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 0, "queue empty");

    lexer_destroy(&lx);
    (void)ctest;
}

/* Push gives an empty context; pop restores the saved one unchanged. */
CTEST(test_context_push_saves_and_pop_restores)
{
    lexer_t *lx = lexer_create();
    queue(lx, "OUTER1", false, false);
    queue(lx, "OUTER2", true, true);
    heredoc_entry_t *outer_entries = lx->heredoc_queue.entries;
    CTEST_ASSERT_TRUE(ctest, lexer_heredoc_pending(lx), "outer here-documents pending");

    lexer_push_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 1, "depth is 1 after push");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 0, "nested queue starts empty");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_index, 0, "nested index starts at 0");
    CTEST_ASSERT_FALSE(ctest, lx->reading_heredoc, "nested context is not reading");
    CTEST_ASSERT_FALSE(ctest, lexer_heredoc_pending(lx), "nothing pending in nested context");
    CTEST_ASSERT_TRUE(ctest, lx->heredoc_queue.entries != outer_entries,
                      "nested context has its own entry array");

    queue(lx, "INNER", false, false);
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 1, "nested queue holds the inner one");

    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 0, "depth is 0 after pop");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 2, "outer queue restored with 2 entries");
    CTEST_ASSERT_TRUE(ctest, lx->heredoc_queue.entries == outer_entries,
                      "outer entry array is the original one");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[0].delimiter), "OUTER1",
                        "first outer delimiter intact");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[1].delimiter), "OUTER2",
                        "second outer delimiter intact");
    CTEST_ASSERT_TRUE(ctest, lx->heredoc_queue.entries[1].strip_tabs, "strip_tabs restored");
    CTEST_ASSERT_TRUE(ctest, lx->heredoc_queue.entries[1].delimiter_quoted, "quoted restored");
    CTEST_ASSERT_TRUE(ctest, lexer_heredoc_pending(lx), "outer here-documents pending again");

    lexer_destroy(&lx);
    (void)ctest;
}

/* The reading position of the saved context survives a push/pop. */
CTEST(test_context_saves_reading_position)
{
    lexer_t *lx = lexer_create();
    queue(lx, "A", false, false);
    queue(lx, "B", false, false);
    lx->heredoc_index = 1;
    lx->reading_heredoc = true;

    lexer_push_heredoc_context(lx);
    CTEST_ASSERT_FALSE(ctest, lx->reading_heredoc, "nested context not reading");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_index, 0, "nested index 0");

    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_TRUE(ctest, lx->reading_heredoc, "reading flag restored");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_index, 1, "index restored");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 2, "queue restored");

    lexer_destroy(&lx);
    (void)ctest;
}

/* Contexts nest: pops come back in reverse order of the pushes. */
CTEST(test_context_nesting_is_lifo)
{
    lexer_t *lx = lexer_create();
    queue(lx, "L0", false, false);
    lexer_push_heredoc_context(lx);
    queue(lx, "L1a", false, false);
    queue(lx, "L1b", false, false);
    lexer_push_heredoc_context(lx);
    queue(lx, "L2", false, false);
    lexer_push_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 3, "three contexts saved");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 0, "innermost context empty");

    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 1, "level 2 has one entry");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[0].delimiter), "L2",
                        "level 2 delimiter");
    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 2, "level 1 has two entries");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[1].delimiter), "L1b",
                        "level 1 delimiter");
    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 1, "level 0 has one entry");
    CTEST_ASSERT_STR_EQ(ctest, string_cstr(lx->heredoc_queue.entries[0].delimiter), "L0",
                        "level 0 delimiter");
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 0, "back at the top level");

    lexer_destroy(&lx);
    (void)ctest;
}

/* Pop discards whatever the nested context still had pending. */
CTEST(test_context_pop_drops_nested_pending)
{
    lexer_t *lx = lexer_create();
    lexer_push_heredoc_context(lx);
    queue(lx, "DROPPED", false, false);
    queue(lx, "ALSO_DROPPED", false, false);
    lx->reading_heredoc = true;

    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 0, "top-level queue is empty");
    CTEST_ASSERT_FALSE(ctest, lx->reading_heredoc, "top level is not reading");
    CTEST_ASSERT_FALSE(ctest, lexer_heredoc_pending(lx), "nothing pending");

    lexer_destroy(&lx);
    (void)ctest;
}

/* Popping at the top level is a harmless no-op. */
CTEST(test_context_pop_at_top_level)
{
    lexer_t *lx = lexer_create();
    queue(lx, "KEPT", false, false);

    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 0, "depth still 0");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 1, "queue untouched");

    lexer_destroy(&lx);
    (void)ctest;
}

/* The stack grows past its initial capacity. */
CTEST(test_context_stack_grows)
{
    lexer_t *lx = lexer_create();
    for (int i = 0; i < 20; i++)
    {
        queue(lx, "D", false, false);
        lexer_push_heredoc_context(lx);
    }
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 20, "20 contexts saved");
    for (int i = 0; i < 20; i++)
        lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 0, "all popped");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 1, "the top-level entry remains");

    lexer_destroy(&lx);
    (void)ctest;
}

/* lexer_reset() returns to the top level and empties it. */
CTEST(test_context_reset_returns_to_top_level)
{
    lexer_t *lx = lexer_create();
    queue(lx, "OUTER", false, false);
    lexer_push_heredoc_context(lx);
    queue(lx, "INNER", false, false);
    lexer_push_heredoc_context(lx);

    lexer_reset(lx);
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 0, "depth is 0 after reset");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 0, "queue empty after reset");
    CTEST_ASSERT_FALSE(ctest, lexer_heredoc_pending(lx), "nothing pending after reset");

    /* the lexer is usable afterwards */
    lexer_append_input_cstr(lx, "cat <<EOF\nx\nEOF\n");
    token_list_t *tokens = token_list_create();
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "lexing works after reset");
    const token_t *end = find_end_of_heredoc(tokens, "EOF");
    CTEST_ASSERT_NOT_NULL(ctest, end, "here-document read after reset");
    if (end)
        CTEST_ASSERT_STR_EQ(ctest, string_cstr(end->heredoc_content), "x\n", "body after reset");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

/* Destroying a lexer with saved contexts frees everything (checked by the
 * sanitizer build; here it must simply not crash). */
CTEST(test_context_destroy_with_saved_contexts)
{
    lexer_t *lx = lexer_create();
    queue(lx, "OUTER", false, false);
    lexer_push_heredoc_context(lx);
    queue(lx, "INNER", false, false);
    lexer_push_heredoc_context(lx);
    queue(lx, "INNERMOST", true, true);

    lexer_destroy(&lx);
    CTEST_ASSERT_NULL(ctest, lx, "lexer destroyed");
    (void)ctest;
}

/* The scenario the stack exists for: an enclosing line has a here-document
 * pending; a nested lex reads a complete here-document of its own; after
 * the pop the enclosing here-document is read as if nothing had happened. */
CTEST(test_context_nested_lex_reads_own_heredoc)
{
    lexer_t *lx = lexer_create();
    token_list_t *tokens = token_list_create();

    /* enclosing line: "cat <<OUTER" has been seen, body not yet read */
    queue(lx, "OUTER", false, false);

    /* nested lex of "cat <<INNER\ninner body\nINNER\n" */
    lexer_push_heredoc_context(lx);
    lexer_append_input_cstr(lx, "cat <<INNER\ninner body\nINNER\n");
    lex_status_t status = lexer_tokenize(lx, tokens, NULL);
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "nested text lexes completely");
    const token_t *inner = find_end_of_heredoc(tokens, "INNER");
    CTEST_ASSERT_NOT_NULL(ctest, inner, "inner here-document was read in the nested context");
    if (inner)
        CTEST_ASSERT_STR_EQ(ctest, string_cstr(inner->heredoc_content), "inner body\n",
                            "inner body is correct");
    CTEST_ASSERT_NULL(ctest, find_end_of_heredoc(tokens, "OUTER"),
                      "the outer here-document was not read inside the nested lex");
    CTEST_ASSERT_FALSE(ctest, lexer_heredoc_pending(lx), "nested context finished");

    /* back to the enclosing line: its here-document is still pending */
    lexer_pop_heredoc_context(lx);
    CTEST_ASSERT_TRUE(ctest, lexer_heredoc_pending(lx), "outer here-document pending after pop");
    CTEST_ASSERT_EQ(ctest, lx->heredoc_queue.size, 1, "outer queue has one entry");

    /* the end of the enclosing line, then the outer body */
    lexer_append_input_cstr(lx, "\nouter body\nOUTER\n");
    status = lexer_tokenize(lx, tokens, NULL);
    CTEST_ASSERT_EQ(ctest, status, LEX_OK, "enclosing text lexes completely");
    const token_t *outer = find_end_of_heredoc(tokens, "OUTER");
    CTEST_ASSERT_NOT_NULL(ctest, outer, "outer here-document read after the pop");
    if (outer)
        CTEST_ASSERT_STR_EQ(ctest, string_cstr(outer->heredoc_content), "outer body\n",
                            "outer body is correct");
    CTEST_ASSERT_FALSE(ctest, lexer_heredoc_pending(lx), "nothing pending at the end");
    CTEST_ASSERT_EQ(ctest, lexer_heredoc_context_depth(lx), 0, "top level at the end");

    token_list_destroy(&tokens);
    lexer_destroy(&lx);
    (void)ctest;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    miga_setjmp();

    CTestEntry *suite[] = {
        CTEST_ENTRY(test_context_initial_state),
        CTEST_ENTRY(test_context_push_saves_and_pop_restores),
        CTEST_ENTRY(test_context_saves_reading_position),
        CTEST_ENTRY(test_context_nesting_is_lifo),
        CTEST_ENTRY(test_context_pop_drops_nested_pending),
        CTEST_ENTRY(test_context_pop_at_top_level),
        CTEST_ENTRY(test_context_stack_grows),
        CTEST_ENTRY(test_context_reset_returns_to_top_level),
        CTEST_ENTRY(test_context_destroy_with_saved_contexts),
        CTEST_ENTRY(test_context_nested_lex_reads_own_heredoc),
        NULL
    };

    int result = ctest_run_suite(suite);

    miga_arena_end();

    return result;
}
