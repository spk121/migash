#ifndef EXEC_PARSE_SESSION_H
#define EXEC_PARSE_SESSION_H

/**
 * @file exec_parse_session.h
 * @brief Unified parse session for the shell executor.
 *
 * An exec_parse_session_t bundles all state needed to incrementally lex,
 * tokenize, and parse shell input:
 *
 *   - A lexer (tracks unclosed quotes, heredocs, etc.)
 *   - A tokenizer (alias expansion, compound-command buffering)
 *   - Accumulated tokens from an incomplete parse
 *   - Line-number tracking
 *   - Source-location metadata for error messages
 *   - An "incomplete" flag for the partial-execution API
 *
 * Previously this state was scattered across exec_string_ctx_t,
 * exec_partial_state_t, and a bare tokenizer_t pointer on exec_t.
 * This structure replaces all three.
 */

#include <stdbool.h>
#include <stddef.h>

#include "string_t.h"

/* Forward declarations — concrete definitions live in their own headers. */
typedef struct lexer_t lexer_t;
typedef struct tokenizer_t tokenizer_t;
typedef struct token_list_t token_list_t;
typedef struct alias_store_t alias_store_t;

/* ============================================================================
 * Parse Session
 * ============================================================================ */

typedef struct exec_parse_session_t
{
    /* Lexer — owns quote / heredoc continuation state. */
    lexer_t *lexer;

    /* Tokenizer — alias expansion, compound-command buffering. */
    tokenizer_t *tokenizer;

    /* Tokens accumulated across lines when the parser returns INCOMPLETE. */
    token_list_t *accumulated_tokens;

    /* Line counter (incremented by exec_string_core on each chunk). */
    int line_num;

    /* --- Fields carried over from exec_partial_state_t --- */

    /* Whether we are mid-parse (unclosed quote, compound command, etc.). */
    bool incomplete;

    /* Source filename for error messages (may be NULL). */
    string_t *filename;

    /* Source line number as seen by the caller (may differ from line_num
       when the caller supplies explicit line numbers). */
    size_t caller_line_number;

} exec_parse_session_t;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

/**
 * Create a new parse session.
 *
 * @param aliases  Alias store for expansion (may be NULL to disable aliases).
 * @return A heap-allocated session, or NULL on allocation failure.
 *         The caller owns the returned session and must destroy it with
 *         exec_parse_session_destroy().
 */
exec_parse_session_t *exec_parse_session_create(alias_store_t *aliases);

/**
 * Destroy a parse session and free all associated memory.
 * Sets *session to NULL.  Safe to call with NULL or *session == NULL.
 */
void exec_parse_session_destroy(exec_parse_session_t **session);

/**
 * Reset a session after a command has been successfully executed.
 *
 * This clears the lexer, accumulated tokens, and resets the tokenizer
 * for the next command, but keeps the session allocated for reuse.
 * The line counter is NOT reset (it keeps incrementing).
 */
void exec_parse_session_reset(exec_parse_session_t *session);

/**
 * Fully reset a session, including destroying and recreating the tokenizer.
 *
 * Used after SIGINT or other hard interrupts where any buffered
 * compound-command state in the tokenizer must be discarded.
 *
 * @param session  The session.
 * @param aliases  Alias store for the new tokenizer (may be NULL).
 */
void exec_parse_session_hard_reset(exec_parse_session_t *session, alias_store_t *aliases);

/* ============================================================================
 * Opaque-size helper (for callers that cannot include this header)
 * ============================================================================ */

/**
 * Return sizeof(exec_parse_session_t) so callers can allocate one
 * without including exec_parse_session.h.
 */
size_t exec_parse_session_size(void);

/**
 * Release all resources held by a session that was allocated by the caller
 * (e.g. stack-allocated or embedded in another struct) rather than by
 * exec_parse_session_create().  After cleanup the struct is zeroed and
 * may be reused.
 *
 * Safe to call on an already-zeroed struct (no-op).
 */
void exec_parse_session_cleanup(exec_parse_session_t *session);

#endif /* EXEC_PARSE_SESSION_H */
