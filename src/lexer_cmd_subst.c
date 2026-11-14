/**
 * @file lexer_cmd_subst.c
 * @brief Lexer module for POSIX shell command substitution
 *
 * This module handles lexing of command substitution:
 * - Parenthesized form: $(command)
 * - Backtick form: `command`
 *
 * Per POSIX specification, command substitution allows the output
 * of a command to replace the command itself. Both forms are
 * functionally equivalent but have different quoting rules.
 *
 * In the $(...) form, all characters are processed normally,
 * following standard shell parsing rules.
 *
 * In the backtick form, backslashes retain their literal meaning
 * except when followed by $, `, \, or newline.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define LEXER_INTERNAL
#include "lexer_cmd_subst.h"

#include "lexer.h"
#include "logging.h"
#include "token.h"

lex_status_t lexer_process_cmd_subst_paren(lexer_t *lx)
{
    Expects_not_null(lx);

    if (!lx->in_word)
        lexer_start_word(lx);

    /* Entered right after "$(" the first time; re-entered after more input
     * was appended when the previous call returned LEX_INCOMPLETE. The text
     * collected so far lives in the lexer, not on this stack frame. */
    if (lx->cmd_subst_text == NULL)
    {
        lx->cmd_subst_text = string_create();
        lx->cmd_subst_depth = 1;
    }
    string_t *cmd_text = lx->cmd_subst_text;

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        if (c == ')')
        {
            if (--lx->cmd_subst_depth == 0)
            {
                lexer_advance(lx); // consume )

                part_t *part = part_create_command_subst(cmd_text);
                string_destroy(&lx->cmd_subst_text);
                lx->cmd_subst_depth = 0;

                if (lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                    part_set_quoted(part, false, true);

                token_add_part(lx->current_token, part);
                lx->current_token->needs_expansion = true;
                if (!lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                    lx->current_token->needs_field_splitting = true;

                lexer_pop_mode(lx);
                return LEX_OK;
            }
        }
        else if (c == '(')
        {
            lx->cmd_subst_depth++;
        }
        else if (c == '\\')
        {
            char next = lexer_peek_ahead(lx, 1);

            // Backslash at EOF - need more input
            if (next == '\0')
            {
                return LEX_INCOMPLETE;
            }

            // Keep the backslash and the escaped character verbatim: the
            // substitution text is lexed again when it is run.
            string_append_char(cmd_text, lexer_advance(lx));
            string_append_char(cmd_text, lexer_advance(lx));
            if (next == '\n' && lexer_at_end(lx))
                return LEX_INCOMPLETE;
            continue;
        }

        string_append_char(cmd_text, lexer_advance(lx));
    }

    return LEX_INCOMPLETE;
}

lex_status_t lexer_process_cmd_subst_backtick(lexer_t *lx)
{
    Expects_not_null(lx);

    // We enter after the opening ` has been consumed
    // Ensure we have a word token to build
    if (!lx->in_word)
    {
        lexer_start_word(lx);
    }

    // Buffer to accumulate raw command text; kept in the lexer so that it
    // survives a LEX_INCOMPLETE return when the backquotes span lines.
    if (lx->cmd_subst_text == NULL)
        lx->cmd_subst_text = string_create();
    string_t *cmd_text = lx->cmd_subst_text;

    // Check if we're inside double quotes (affects backslash handling)
    bool in_dquote = lexer_in_mode(lx, LEX_DOUBLE_QUOTE);

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        // Handle closing backtick
        if (c == '`')
        {
            // Found the closing backtick
            lexer_advance(lx); // consume `

            // Add command substitution part to current token
            part_t *part = part_create_command_subst(cmd_text);
            string_destroy(&lx->cmd_subst_text);

            // Mark as double-quoted if inside double quotes
            if (in_dquote)
            {
                part_set_quoted(part, false, true);
            }

            token_add_part(lx->current_token, part);
            lx->current_token->needs_expansion = true;

            // Field splitting only if not in double quotes
            if (!in_dquote)
            {
                lx->current_token->needs_field_splitting = true;
            }

            lexer_pop_mode(lx);
            return LEX_OK;
        }

        in_dquote = lexer_in_mode(lx, LEX_DOUBLE_QUOTE);

        if (c == '\\')
        {
            char next = lexer_peek_ahead(lx, 1);
            if (next == '\0')
                return LEX_INCOMPLETE;

            lexer_advance(lx); // consume <backslash>

            if (next == '\n')
            {
                // Line continuation
                lexer_advance(lx);
                lx->line_no++;
                lx->col_no = 1;
                // After line continuation, check if we're at EOF
                if (lexer_at_end(lx))
                {
                    return LEX_INCOMPLETE;
                }
                continue;
            }

            // These are always escaped in backticks
            if (next == '$' || next == '`' || next == '\\')
            {
                string_append_char(cmd_text, next);
                lexer_advance(lx);
                continue;
            }

            // " is only escaped if NOT in double quotes
            if (next == '"' && !in_dquote)
            {
                string_append_char(cmd_text, '"');
                lexer_advance(lx);
                continue;
            }

            // Otherwise: keep both \ and next
            string_append_char(cmd_text, '\\');
            string_append_char(cmd_text, next);
            lexer_advance(lx);
            continue;
        }

        // All other characters - add to command text
        string_append_char(cmd_text, c);
        lexer_advance(lx);
    }

    // End of input without closing backtick: keep what we have for the next line
    return LEX_INCOMPLETE;
}
