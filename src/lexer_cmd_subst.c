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
        lx->cmd_subst_sq = false;
        lx->cmd_subst_dq = false;
        lx->cmd_subst_case_count = 0;
        lx->cmd_subst_hd_line = NULL;
        /* Here-documents inside the substitution belong to it: their
         * delimiters are queued in a context of their own, and the enclosing
         * line's pending here-documents wait until the closing ")". */
        lexer_push_heredoc_context(lx);
    }
    string_t *cmd_text = lx->cmd_subst_text;

    while (!lexer_at_end(lx))
    {
        char c = lexer_peek(lx);

        /* Inside a here-document body: copy lines verbatim until the
         * delimiter line. Quotes and parentheses mean nothing there. */
        if (lx->reading_heredoc)
        {
            const heredoc_entry_t *entry = &lx->heredoc_queue.entries[lx->heredoc_index];
            if (!lx->cmd_subst_hd_line)
                lx->cmd_subst_hd_line = string_create();
            string_append_char(cmd_text, c);
            if (c == '\n')
            {
                const char *line = string_cstr(lx->cmd_subst_hd_line);
                if (entry->strip_tabs)
                    while (*line == '\t')
                        line++;
                if (strcmp(line, string_cstr(entry->delimiter)) == 0)
                {
                    /* the next queued body follows at once, if any */
                    lx->heredoc_index++;
                    if (lx->heredoc_index >= lx->heredoc_queue.size)
                    {
                        lx->reading_heredoc = false;
                        lexer_empty_heredoc_queue(lx);
                        lx->heredoc_index = 0;
                    }
                }
                string_clear(lx->cmd_subst_hd_line);
            }
            else
                string_append_char(lx->cmd_subst_hd_line, c);
            lexer_advance(lx);
            continue;
        }

        /* A newline after one or more "<<" operators starts their bodies */
        if (c == '\n' && !lx->cmd_subst_sq && !lx->cmd_subst_dq && lx->heredoc_queue.size > 0)
        {
            string_append_char(cmd_text, lexer_advance(lx));
            lx->reading_heredoc = true;
            lx->heredoc_index = 0;
            if (lx->cmd_subst_hd_line)
                string_clear(lx->cmd_subst_hd_line);
            continue;
        }

        /* "<<" or "<<-" outside quotes: remember the delimiter word */
        if (c == '<' && !lx->cmd_subst_sq && !lx->cmd_subst_dq && lexer_peek_ahead(lx, 1) == '<')
        {
            string_append_char(cmd_text, lexer_advance(lx));
            string_append_char(cmd_text, lexer_advance(lx));
            bool strip = false;
            bool quoted = false;
            if (lexer_peek(lx) == '-')
            {
                strip = true;
                string_append_char(cmd_text, lexer_advance(lx));
            }
            while (lexer_peek(lx) == ' ' || lexer_peek(lx) == '\t')
                string_append_char(cmd_text, lexer_advance(lx));
            string_t *delim = string_create();
            while (!lexer_at_end(lx))
            {
                char d = lexer_peek(lx);
                if (d == ' ' || d == '\t' || d == '\n' || d == ';' || d == ')' || d == '|' ||
                    d == '&' || d == '<' || d == '>')
                    break;
                if (d == '\'' || d == '"')
                {
                    /* quoted part of the delimiter: literal up to the closing quote */
                    quoted = true;
                    string_append_char(cmd_text, lexer_advance(lx));
                    while (!lexer_at_end(lx) && lexer_peek(lx) != d)
                    {
                        char q = lexer_advance(lx);
                        string_append_char(delim, q);
                        string_append_char(cmd_text, q);
                    }
                    if (!lexer_at_end(lx))
                        string_append_char(cmd_text, lexer_advance(lx));
                    continue;
                }
                if (d == '\\')
                {
                    quoted = true;
                    string_append_char(cmd_text, lexer_advance(lx));
                    if (lexer_at_end(lx))
                        break;
                    d = lexer_peek(lx);
                }
                string_append_char(delim, d);
                string_append_char(cmd_text, lexer_advance(lx));
            }
            if (lexer_at_end(lx))
            {
                /* delimiter cut by end of input: wait for more */
                string_destroy(&delim);
                return LEX_INCOMPLETE;
            }
            lexer_queue_heredoc(lx, delim, strip, quoted);
            string_destroy(&delim);
            continue;
        }

        /* A comment runs to the end of the line; quotes in it mean nothing */
        if (c == '#' && !lx->cmd_subst_sq && !lx->cmd_subst_dq)
        {
            int len = string_length(cmd_text);
            char prev = len > 0 ? string_at(cmd_text, len - 1) : ' ';
            if (prev == ' ' || prev == '\t' || prev == '\n' || prev == ';' || prev == '(' ||
                prev == '&' || prev == '|')
            {
                while (!lexer_at_end(lx) && lexer_peek(lx) != '\n')
                    string_append_char(cmd_text, lexer_advance(lx));
                continue;
            }
        }

        /* Quoted text is copied verbatim: parentheses inside quotes do not
         * nest. (A "$(" inside double quotes still nests, below.) */
        if (lx->cmd_subst_sq)
        {
            if (c == '\'')
                lx->cmd_subst_sq = false;
            string_append_char(cmd_text, lexer_advance(lx));
            continue;
        }
        if (c == '\'' && !lx->cmd_subst_dq)
        {
            lx->cmd_subst_sq = true;
            string_append_char(cmd_text, lexer_advance(lx));
            continue;
        }
        if (c == '"')
        {
            lx->cmd_subst_dq = !lx->cmd_subst_dq;
            string_append_char(cmd_text, lexer_advance(lx));
            continue;
        }

        /* "case ... in pattern) ..." : the ')' after a pattern closes nothing.
         * Remember the paren depth at each "case" word; a ')' seen at that
         * depth is a pattern terminator until the matching "esac". */
        if (!lx->cmd_subst_dq && (c == 'c' || c == 'e'))
        {
            int len = string_length(cmd_text);
            char prev = len > 0 ? string_at(cmd_text, len - 1) : ' ';
            bool at_word_start = prev == ' ' || prev == '\t' || prev == '\n' || prev == ';' ||
                                 prev == '(' || prev == '&' || prev == '|';
            if (at_word_start && lexer_input_starts_with(lx, "case") &&
                (lexer_peek_ahead(lx, 4) == ' ' || lexer_peek_ahead(lx, 4) == '\t'))
            {
                if (lx->cmd_subst_case_count < 16)
                    lx->cmd_subst_case_depth[lx->cmd_subst_case_count] = lx->cmd_subst_depth;
                lx->cmd_subst_case_count++;
            }
            else if (at_word_start && lexer_input_starts_with(lx, "esac"))
            {
                char after = lexer_peek_ahead(lx, 4);
                if (after == '\0' || after == ' ' || after == '\t' || after == '\n' || after == ';' ||
                    after == ')' || after == '&' || after == '|')
                {
                    if (lx->cmd_subst_case_count > 0)
                        lx->cmd_subst_case_count--;
                }
            }
        }

        if (c == ')')
        {
            int n = lx->cmd_subst_case_count;
            if (n > 0 && n <= 16 && lx->cmd_subst_case_depth[n - 1] == lx->cmd_subst_depth)
            {
                /* pattern terminator inside case ... esac */
                string_append_char(cmd_text, lexer_advance(lx));
                continue;
            }
            if (--lx->cmd_subst_depth == 0)
            {
                lexer_advance(lx); // consume )

                part_t *part = part_create_command_subst(cmd_text);
                string_destroy(&lx->cmd_subst_text);
                lx->cmd_subst_depth = 0;
                /* back to the enclosing line's here-documents; anything the
                 * substitution left unterminated is dropped */
                lexer_pop_heredoc_context(lx);
                if (lx->cmd_subst_hd_line)
                    string_destroy(&lx->cmd_subst_hd_line);

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
