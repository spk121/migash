/**
 * @file exec_parse_session.c
 * @brief Implementation of the unified parse session.
 */

#include "exec_parse_session.h"

#include "lexer.h"
#include "token.h"
#include "tokenizer.h"
#include "string_t.h"
#include "xalloc.h"

#include <string.h>

 /* ============================================================================
  * Lifecycle
  * ============================================================================ */

exec_parse_session_t* exec_parse_session_create(alias_store_t* aliases)
{
    exec_parse_session_t* s = xcalloc(1, sizeof(*s));

    s->lexer = lexer_create();
    if (!s->lexer)
    {
        xfree(s);
        return NULL;
    }

    s->tokenizer = tokenizer_create(aliases);
    if (!s->tokenizer)
    {
        lexer_destroy(&s->lexer);
        xfree(s);
        return NULL;
    }

    s->accumulated_tokens = NULL;
    s->line_num = 0;
    s->incomplete = false;
    s->filename = NULL;
    s->caller_line_number = 0;

    return s;
}

void exec_parse_session_destroy(exec_parse_session_t** session)
{
    if (!session || !*session)
        return;

    exec_parse_session_t* s = *session;

    if (s->lexer)
        lexer_destroy(&s->lexer);
    if (s->tokenizer)
        tokenizer_destroy(&s->tokenizer);
    if (s->accumulated_tokens)
        token_list_destroy(&s->accumulated_tokens);
    if (s->filename)
        string_destroy(&s->filename);

    xfree(s);
    *session = NULL;
}

void exec_parse_session_reset(exec_parse_session_t* session)
{
    if (!session)
        return;

    if (session->lexer)
        lexer_reset(session->lexer);
    if (session->accumulated_tokens)
    {
        token_list_destroy(&session->accumulated_tokens);
        session->accumulated_tokens = NULL;
    }
    if (session->tokenizer)
        tokenizer_reset(session->tokenizer);

    session->incomplete = false;
}

void exec_parse_session_hard_reset(exec_parse_session_t* session, alias_store_t* aliases)
{
    if (!session)
        return;

    /* Reset the lexer. */
    if (session->lexer)
        lexer_reset(session->lexer);

    /* Discard accumulated tokens. */
    if (session->accumulated_tokens)
    {
        token_list_destroy(&session->accumulated_tokens);
        session->accumulated_tokens = NULL;
    }

    /* Destroy and recreate the tokenizer to flush any buffered
       compound-command tokens. */
    if (session->tokenizer)
        tokenizer_destroy(&session->tokenizer);
    session->tokenizer = tokenizer_create(aliases);

    session->incomplete = false;
}

/* ============================================================================
 * Opaque-size helpers
 * ============================================================================ */

size_t exec_parse_session_size(void)
{
    return sizeof(exec_parse_session_t);
}

void exec_parse_session_cleanup(exec_parse_session_t* session)
{
    if (!session)
        return;

    if (session->lexer)
        lexer_destroy(&session->lexer);
    if (session->tokenizer)
        tokenizer_destroy(&session->tokenizer);
    if (session->accumulated_tokens)
        token_list_destroy(&session->accumulated_tokens);
    if (session->filename)
        string_destroy(&session->filename);

    memset(session, 0, sizeof(*session));
}
