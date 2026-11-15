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
 * In the $(...) form the text is lexed by the normal-mode lexer itself (a
 * recursive lex) to find the closing parenthesis; the word keeps the source
 * text, which is lexed again when the substitution is run.
 *
 * In the backtick form, backslashes retain their literal meaning
 * except when followed by $, `, \, or newline.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define LEXER_INTERNAL
#include "lexer_cmd_subst.h"

#include <string.h>

#include "lexer.h"
#include "lexer_normal.h"
#include "logging.h"
#include "miga/xalloc.h"
#include "token.h"

/* ============================================================================
 * $( ... ): a recursive lex
 *
 * The substitution's text is lexed by the normal-mode lexer; this handler
 * only counts parentheses to find the closing one. Quotes, comments,
 * parameter and arithmetic expansions, backticks, here-documents and nested
 * "$(" are handled by the modes that own them, which the dispatcher runs on
 * top of this one. The tokens produced inside are thrown away at the close;
 * what the word keeps is the source text, which is lexed again when the
 * substitution is run. "case" patterns are the one grammatical fact needed
 * here: a ")" that ends a pattern closes nothing, so the paren depth at each
 * "case" (at a command position) is remembered until its "esac".
 * ============================================================================ */

static cmd_subst_ctx_t *cmd_subst_top(lexer_t *lx)
{
    Expects_gt(lx->cmd_subst_count, 0);
    return &lx->cmd_subst_stack[lx->cmd_subst_count - 1];
}

static bool case_open_at_depth(const cmd_subst_ctx_t *ctx, int depth)
{
    return ctx->case_count > 0 && ctx->case_count <= 16 &&
           ctx->case_depth[ctx->case_count - 1] == depth;
}

void lexer_begin_cmd_subst_paren(lexer_t *lx)
{
    Expects_not_null(lx);

    /* the word that holds the substitution */
    if (!lx->in_word)
        lexer_start_word(lx);

    if (lx->cmd_subst_count >= lx->cmd_subst_capacity)
    {
        int newcap = lx->cmd_subst_capacity ? lx->cmd_subst_capacity * 2 : 4;
        lx->cmd_subst_stack =
            xrealloc(lx->cmd_subst_stack, (size_t)newcap * sizeof(cmd_subst_ctx_t));
        lx->cmd_subst_capacity = newcap;
    }
    cmd_subst_ctx_t *ctx = &lx->cmd_subst_stack[lx->cmd_subst_count++];
    ctx->start_pos = lx->pos;
    ctx->depth = 0;
    ctx->token_count = token_list_size(lx->tokens);
    ctx->saved_token = lx->current_token;
    ctx->saved_in_word = lx->in_word;
    ctx->saved_at_command_start = lx->at_command_start;
    ctx->saved_after_case_in = lx->after_case_in;
    ctx->case_count = 0;

    /* the nested lex starts clean, at a command position */
    lx->current_token = NULL;
    lx->in_word = false;
    lx->at_command_start = true;
    lx->after_case_in = false;

    lexer_push_heredoc_context(lx);
    lexer_push_mode(lx, LEX_CMD_SUBST_PAREN);
}

/* Leave the innermost substitution: restore the enclosing word and state.
 * The nested tokens and any nested word in progress are destroyed. */
static void cmd_subst_leave(lexer_t *lx, cmd_subst_ctx_t *ctx)
{
    lexer_discard_tokens_above(lx, ctx->token_count);
    if (lx->current_token)
        token_destroy(&lx->current_token);
    lx->current_token = ctx->saved_token;
    lx->in_word = ctx->saved_in_word;
    lx->at_command_start = ctx->saved_at_command_start;
    lx->after_case_in = ctx->saved_after_case_in;
    lexer_pop_heredoc_context(lx);
    lx->cmd_subst_count--;
}

void lexer_abandon_cmd_substs(lexer_t *lx)
{
    Expects_not_null(lx);
    while (lx->cmd_subst_count > 0)
        cmd_subst_leave(lx, cmd_subst_top(lx));
}

/* After the normal lexer produced a token: note "case" and "esac" words at
 * a command position. */
static void note_case_words(lexer_t *lx, cmd_subst_ctx_t *ctx, int before, bool at_command)
{
    /* The normal lexer may produce several tokens in one call ("case x in
     * a" before stopping at the ")"), so the command-position rule is
     * replayed over all of them, starting from the position before the call. */
    int size = token_list_size(lx->tokens);
    for (int i = before; i < size; i++)
    {
        const token_t *tok = token_list_get(lx->tokens, i);
        if (token_get_type(tok) == TOKEN_WORD)
        {
            const char *w = lexer_literal_word_text(tok);
            if (at_command && w && strcmp(w, "case") == 0)
            {
                if (ctx->case_count < 16)
                    ctx->case_depth[ctx->case_count] = ctx->depth;
                ctx->case_count++;
            }
            else if (at_command && w && strcmp(w, "esac") == 0)
            {
                if (ctx->case_count > 0)
                    ctx->case_count--;
            }
            at_command = at_command && lexer_word_keeps_command_start(tok);
        }
        else if (token_get_type(tok) != TOKEN_EOF)
            at_command = lexer_operator_starts_command(token_get_type(tok));
    }
}

lex_status_t lexer_process_cmd_subst_paren(lexer_t *lx)
{
    Expects_not_null(lx);
    cmd_subst_ctx_t *ctx = cmd_subst_top(lx);

    for (;;)
    {
        /* Only the normal lexer may decide that the input is over; here the
         * end of the input means that more is needed. */
        if (lexer_at_end(lx))
            return LEX_INCOMPLETE;

        char c = lexer_peek(lx);
        if (c == ')')
        {
            if (ctx->depth == 0 && !case_open_at_depth(ctx, 0))
            {
                /* the closing parenthesis */
                if (lx->in_word)
                    lexer_finalize_word(lx);
                int end_pos = lx->pos;
                lexer_advance(lx);

                string_t *text = string_substring(lx->input, ctx->start_pos, end_pos);
                cmd_subst_leave(lx, ctx);
                lexer_pop_mode(lx);

                part_t *part = part_create_command_subst(text);
                string_destroy(&text);
                if (lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                    part_set_quoted(part, false, true);
                token_add_part(lx->current_token, part);
                lx->current_token->needs_expansion = true;
                if (!lexer_in_mode(lx, LEX_DOUBLE_QUOTE))
                    lx->current_token->needs_field_splitting = true;
                return LEX_OK;
            }
            /* a pattern terminator inside case ... esac closes nothing;
             * any other ")" closes a "(" of the substitution's own. The
             * normal lexer leaves parentheses to this handler. */
            if (!case_open_at_depth(ctx, ctx->depth))
                ctx->depth--;
            if (lx->in_word)
                lexer_finalize_word(lx);
            lexer_advance(lx);
            lexer_emit_token(lx, TOKEN_RPAREN);
            continue;
        }
        else if (c == '(')
        {
            ctx->depth++;
            if (lx->in_word)
                lexer_finalize_word(lx);
            lexer_advance(lx);
            lexer_emit_token(lx, TOKEN_LPAREN);
            continue;
        }

        int before = token_list_size(lx->tokens);
        bool at_command = lx->at_command_start;
        lex_status_t status = lexer_process_one_normal_token(lx);
        if (status == LEX_ERROR || status == LEX_INTERNAL_ERROR)
            return status;
        note_case_words(lx, ctx, before, at_command);
        if (status == LEX_INCOMPLETE)
        {
            /* a mode was pushed (quote, expansion, here-document body, a
             * nested "$("), or more input is needed; the dispatcher runs
             * that mode and comes back here */
            return LEX_INCOMPLETE;
        }
        /* LEX_OK: a token or some blanks; go on with the next */
    }
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
