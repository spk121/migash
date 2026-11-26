#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>



#include "glob_util.h"

#include "logging.h"
#include "miga/strlist.h"
#include "miga/string_t.h"

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * Check whether position `s` within `string` is a "leading" position for the
 * purpose of the GLOB_UTIL_PERIOD flag.
 *
 * A position is leading if it is the very start of the string, or (when
 * GLOB_UTIL_PATHNAME is also set) immediately after a '/'.
 */
static inline int is_leading_position(const char *s, const char *string, int flags)
{
    if (s == string)
        return 1;
    if ((flags & GLOB_UTIL_PATHNAME) && s > string && s[-1] == '/')
        return 1;
    return 0;
}

/**
 * Check whether the character at position `s` is a leading dot that must be
 * matched explicitly (i.e. GLOB_UTIL_PERIOD is set, `*s == '.'`, and the
 * position is leading).
 */
static inline int is_protected_dot(const char *s, const char *string, int flags)
{
    return (flags & GLOB_UTIL_PERIOD) && *s == '.' && is_leading_position(s, string, flags);
}

/* ============================================================================
 * Pattern Matching Implementation
 * ============================================================================ */


/**
 * Helper: Compare two characters, optionally case-insensitive.
 * Returns true if characters match (according to flags).
 */
static inline int chars_match(char c1, char c2, int flags)
{
    if (c1 == c2)
        return 1;
    if (flags & GLOB_UTIL_CASEFOLD)
    {
        // Case-insensitive: convert to lowercase and compare
        char lower_c1 = (c1 >= 'A' && c1 <= 'Z') ? (char)(c1 - 'A' + 'a') : c1;
        char lower_c2 = (c2 >= 'A' && c2 <= 'Z') ? (char)(c2 - 'A' + 'a') : c2;
        return lower_c1 == lower_c2;
    }
    return 0;
}

/**
 * Helper: Check whether character `c` falls within the range [start, end].
 * Case-insensitive if GLOB_UTIL_CASEFOLD is set.
 */
static inline int char_in_range(char c, char start, char end, int flags)
{
    if (flags & GLOB_UTIL_CASEFOLD)
    {
        // Normalize to lowercase for comparison
        char lower_c = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        char lower_start = (start >= 'A' && start <= 'Z') ? (char)(start - 'A' + 'a') : start;
        char lower_end = (end >= 'A' && end <= 'Z') ? (char)(end - 'A' + 'a') : end;
        return lower_c >= lower_start && lower_c <= lower_end;
    }
    return c >= start && c <= end;
}

/**
 * Helper: Read one (possibly backslash-escaped) character from the pattern.
 *
 * On return, *out contains the literal character and *pp is advanced past it
 * (and past the backslash, if any).  Returns 1 if the character was escaped,
 * 0 otherwise.  If the pattern is exhausted (points to '\0'), returns -1 and
 * *out is undefined.
 */
static inline int read_pattern_char(const char **pp, char *out, int flags)
{
    const char *p = *pp;

    if (*p == '\0')
        return -1;

    if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1] != '\0')
    {
        p++;         // skip backslash
        *out = *p++; // consume the escaped character
        *pp = p;
        return 1; // was escaped
    }

    *out = *p++;
    *pp = p;
    return 0; // not escaped
}

/**
 * Parse and evaluate a bracket expression ([...]) against character `sc`.
 *
 * On entry, `*pp` must point to the first character *after* the opening '['.
 * On return, `*pp` points to the character after the closing ']', or to the
 * end of the pattern if no closing ']' was found (treating the whole thing as
 * a failed match — POSIX says the result is unspecified, but this is a common
 * behaviour).
 *
 * Returns 1 if `sc` matches the bracket expression, 0 otherwise.
 */
static int match_bracket(const char **pp, char sc, int flags)
{
    const char *p = *pp;
    int negate = 0;
    int matched = 0;

    // Check for negation prefix: [!...] or [^...]
    if (*p == '!' || *p == '^')
    {
        negate = 1;
        p++;
    }

    // POSIX: ']' immediately after '[' (or '[!' / '[^') is a literal ']', not
    // the end of the class.
    if (*p == ']')
    {
        if (chars_match(sc, ']', flags))
            matched = 1;
        p++;
    }

    // Scan the rest of the bracket expression.
    while (*p != '\0')
    {
        char ch;
        int escaped = read_pattern_char(&p, &ch, flags);
        if (escaped < 0)
            break; // hit '\0'

        // Unescaped ']' terminates the class.
        if (!escaped && ch == ']')
        {
            *pp = p; // past the ']'
            return matched != negate;
        }

        // Check for range:  <ch> '-' <end>
        // A '-' is a range operator only when:
        //   - It is not escaped.
        //   - The character after '-' is not ']' and not '\0'.
        // Otherwise '-' is literal.
        if (*p == '-' && p[1] != ']' && p[1] != '\0')
        {
            p++; // skip '-'

            // Read the range endpoint, handling a possible backslash escape.
            char end_ch;
            if (read_pattern_char(&p, &end_ch, flags) < 0)
                break; // hit '\0' unexpectedly

            if (char_in_range(sc, ch, end_ch, flags))
                matched = 1;
        }
        else
        {
            // Single character match.
            if (chars_match(sc, ch, flags))
                matched = 1;
        }
    }

    // If we get here we never found a closing ']'.  POSIX says the result is
    // unspecified; we treat the '[' as a literal that doesn't match.
    *pp = p;
    return 0;
}

/**
 * Custom fnmatch implementation for non-POSIX platforms.
 *
 * This is a comprehensive implementation that supports:
 * - Wildcards: * (zero or more), ? (exactly one)
 * - Character classes: [abc], [a-z], [!abc], [^abc]
 * - Backtracking for proper * matching
 * - Flags: GLOB_UTIL_PATHNAME, GLOB_UTIL_PERIOD, GLOB_UTIL_NOESCAPE, GLOB_UTIL_CASEFOLD
 *
 * Conformance notes vs POSIX fnmatch():
 * - Locale-dependent collation is not supported; ranges use byte values.
 * - Named character classes like [:alpha:] are not supported.
 */
bool glob_util_match(const char *pattern, const char *string, int flags)
{
    const char *p = pattern;
    const char *s = string;
    const char *star_pattern = NULL;
    const char *star_string = NULL;

    while (*s)
    {
        // ---- Protected leading dot: only an explicit literal '.' may match.
        // We check this first so that *, ?, and [...] all consistently fail
        // against a leading dot when GLOB_UTIL_PERIOD is active.
        if (is_protected_dot(s, string, flags))
        {
            // Consume any leading '*'s in the pattern — they cannot match the
            // dot, and we must not record them as backtrack points (the dot
            // will still be here if we ever backtrack to this position).
            while (*p == '*')
                p++;

            // The only thing that can match a protected dot is a literal '.'.
            // (An escaped '\.' also counts as literal.)
            if (*p == '.')
            {
                p++;
                s++;
                continue;
            }
            if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1] == '.')
            {
                p += 2;
                s++;
                continue;
            }
            // Nothing else matches a protected dot.
            return false;
        }

        // ---- Escape sequences (unless GLOB_UTIL_NOESCAPE)
        if (!(flags & GLOB_UTIL_NOESCAPE) && *p == '\\' && p[1])
        {
            p++; // skip backslash
            if (chars_match(*p, *s, flags))
            {
                p++;
                s++;
            }
            else
            {
                goto backtrack;
            }
        }
        // ---- Wildcard: *
        else if (*p == '*')
        {
            // With GLOB_UTIL_PATHNAME, '*' does not match '/'.
            // If *s is '/', don't record a backtrack point — fall through so
            // that a literal '/' in the pattern can match it.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                goto backtrack;

            // Record backtrack point and skip the '*'.
            star_pattern = p++;
            star_string = s;
        }
        // ---- Wildcard: ?
        else if (*p == '?')
        {
            // With GLOB_UTIL_PATHNAME, '?' does not match '/'.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                goto backtrack;

            p++;
            s++;
        }
        // ---- Character class: [...]
        else if (*p == '[')
        {
            // With GLOB_UTIL_PATHNAME, bracket expressions do not match '/'.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                goto backtrack;

            p++; // skip '['
            if (!match_bracket(&p, *s, flags))
                goto backtrack;

            s++;
        }
        // ---- Literal character match
        else if (chars_match(*p, *s, flags))
        {
            p++;
            s++;
        }
        else
        {
        backtrack:
            // Mismatch — try advancing the most recent '*' by one character.
            if (!star_pattern)
                return false;

            p = star_pattern + 1;
            s = ++star_string;

            // Check whether the new string position is past end-of-string.
            if (*s == '\0')
                break;

            // With GLOB_UTIL_PATHNAME, '*' cannot consume '/'.
            if ((flags & GLOB_UTIL_PATHNAME) && *s == '/')
                return false;

            // Note: we do NOT check for a protected dot here and return false.
            // Instead we let the next iteration of the main loop handle it via
            // the is_protected_dot() check at the top, which will correctly
            // skip over any '*'s and require a literal '.'.  Returning false
            // here was incorrect because the pattern may well have a literal
            // '.' that matches.
        }
    }

    // String exhausted — skip any trailing '*'s in the pattern.
    while (*p == '*')
        p++;

    return (*p == '\0');
}


bool glob_util_match_str(const string_t *pattern, const string_t *string, int flags)
{
    if (!pattern || !string)
        return false;

    return glob_util_match(string_cstr(pattern), string_cstr(string), flags);
}

/* ============================================================================
 * Pathname Expansion Implementation
 * ============================================================================ */

/* MIGA_ISO_C */
strlist_t *glob_util_expand_path(const string_t *pattern)
{
    (void)pattern;
    log_warn("glob_util_expand_path: No glob implementation available in MIGA_ISO_C mode");
    return NULL;
}

strlist_t *glob_util_expand_path_ex(const string_t *pattern, int flags, const char *base_dir)
{
    // For now, ignore flags and base_dir - future enhancement
    (void)flags;
    (void)base_dir;

    if (base_dir != NULL)
    {
        log_warn("glob_util_expand_path_ex: base_dir parameter not yet implemented");
    }

    return glob_util_expand_path(pattern);
}
