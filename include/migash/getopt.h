/* getopt.h -- Portable GNU-like getopt + getopt_long tweaked for
   use with ISO C and POSIX shell environments.

   Copyright (C) 1987-2025 Free Software Foundation, Inc.
   Copyright (C) 2025 Michael L. Gran.

   This program is free software: you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation, version 2.1 or later. */

/**
 * @file getopt.h
 * @brief Portable GNU-like getopt, getopt_long, and +prefix extensions for
 *        ISO C23 and POSIX shell environments.
 *
 * This header provides the standard POSIX/GNU getopt family of functions for
 * command-line option parsing, plus extensions that recognize a @c + prefix on
 * options.  The +prefix convention is used in POSIX shell builtins (e.g.
 * @c set) where @c -x enables an option and @c +x disables (unsets) it.
 *
 * Three tiers of API are offered:
 *
 *   - **Standard (non-reentrant):** getopt(), getopt_long(),
 *     getopt_long_only().  These use the traditional global variables
 *     @c optarg, @c optind, @c opterr, @c optopt.
 *
 *   - **Plus-aware (non-reentrant):** getopt_long_plus(),
 *     getopt_long_only_plus().  These accept @c struct @c option_ex tables
 *     and recognise @c +x / @c ++name prefixes for toggling boolean flags.
 *
 *   - **Reentrant (@c _r) variants** of all of the above.  These take an
 *     explicit @c struct @c getopt_state instead of global state, making them
 *     safe for concurrent or nested option parsing.
 *
 * @par Optstring syntax
 * The @p optstring argument follows the GNU/POSIX convention:
 *   - A letter alone (e.g. @c "v") is a flag with no argument.
 *   - A letter followed by @c ':' (e.g. @c "o:") requires an argument.
 *   - A letter followed by @c '::' (e.g. @c "c::") accepts an optional
 *     argument (the argument must be attached: @c -cARG, not @c -c @c ARG).
 *   - A leading @c '+' in @p optstring selects REQUIRE_ORDER (stop at the
 *     first non-option).
 *   - A leading @c '-' selects RETURN_IN_ORDER (return non-options as if they
 *     were arguments to option character @c \\1).
 *   - A leading @c ':' (after any @c '+' or @c '-') suppresses error messages
 *     and causes a missing required argument to return @c ':' instead of
 *     @c '?'.
 *   - The sequence @c "W;" causes @c -W @c foo to be treated as the long
 *     option @c --foo.
 *
 * @par Option termination
 *   - The argument @c "--" terminates option scanning; @c optind is advanced
 *     past it and the remaining elements are operands.
 *   - A lone @c "-" is not treated as an option; it is an operand.
 *
 * @par Plus-prefix semantics
 * When using the plus-aware functions (getopt_long_plus(), etc.):
 *   - Short options may be prefixed with @c + instead of @c - (e.g. @c +v).
 *     A @c + prefix means "unset" or "disable".
 *   - Long options may use @c ++ instead of @c -- (e.g. @c ++verbose).
 *   - Whether a particular option accepts a @c + prefix is controlled by the
 *     @c allow_plus field in @c struct @c option_ex.
 *   - When a @c + prefix is used on an option whose @c flag pointer is
 *     non-NULL, the flag is cleared to 0 (instead of being set to @c val).
 */

#ifndef MGSH_GETOPT_H
#define MGSH_GETOPT_H

#include "migash/api.h"
#include "migash/strlist.h"
#include "migash/string_t.h"

MGSH_EXTERN_C_START

/**
 * @name Argument-requirement constants
 * Used in the @c has_arg field of @c struct @c option and @c struct
 * @c option_ex.
 * @{
 */
enum
{
    no_argument = 0,       /**< The option takes no argument. */
    required_argument = 1, /**< The option requires an argument. */
    optional_argument = 2  /**< The option accepts an optional argument
                                (must be attached: @c --opt=ARG or @c -oARG). */
};
/** @} */

/**
 * @brief Describes a single long option for getopt_long() and
 *        getopt_long_only().
 *
 * An array of these structures, terminated by an entry whose @c name is
 * @c NULL, is passed to the long-option functions.
 *
 * @par Example
 * @code
 * static struct option longopts[] = {
 *     { "verbose", no_argument,       &verbose_flag, 1   },
 *     { "output",  required_argument, NULL,          'o' },
 *     { "count",   optional_argument, NULL,          'c' },
 *     { NULL,      0,                 NULL,           0  }
 * };
 * @endcode
 */
struct option
{
    const char *name; /**< Long option name (without leading dashes). */
    int has_arg;      /**< One of @c no_argument, @c required_argument,
                           or @c optional_argument. */
    int *flag;        /**< If non-NULL, getopt_long() sets @c *flag to @c val
                           and returns 0.  If NULL, getopt_long() returns
                           @c val. */
    int val;          /**< Value to return (or to store in @c *flag). */
};

/**
 * @brief Extended option descriptor for the plus-aware long-option functions.
 *
 * This structure extends @c struct @c option with fields that control the
 * @c + prefix ("unset") behaviour used by POSIX shell builtins.
 *
 * @note The first four fields (@c name, @c has_arg, a padding field, and
 *       @c flag / @c val) are laid out so that an array of @c option_ex can
 *       be cast to @c const @c struct @c option* for use with the standard
 *       long-option functions.  The @c allow_plus field occupies the position
 *       that would be @c flag in @c struct @c option, so the cast is only
 *       valid when the plus-aware functions are used.
 *
 * @par Behaviour with @c + prefix
 * When the user supplies a @c + prefix (e.g. @c +v or @c ++verbose):
 *   - If @c allow_plus is 0, the option is rejected and @c '?' is returned.
 *   - If @c allow_plus is 1 and @c flag is non-NULL, @c *flag is set to 0
 *     (cleared) and the function returns 0.
 *   - If @c plus_used is non-NULL, @c *plus_used is set to 1.
 *
 * When the user supplies the normal @c - prefix:
 *   - If @c flag is non-NULL, @c *flag is set to 1 and the function returns
 *     @c val.
 *   - If @c plus_used is non-NULL, @c *plus_used is set to 0.
 *
 * @par Example
 * @code
 * int verbose_flag = 0;
 * int verbose_plus = 0;
 * static struct option_ex longopts[] = {
 *     //  name       has_arg          allow_plus flag           val  plus_used
 *     { "verbose", no_argument,       1,         &verbose_flag, 'v', &verbose_plus },
 *     { "output",  required_argument, 0,         NULL,          'o', NULL          },
 *     { NULL,      0,                 0,         NULL,           0,  NULL          }
 * };
 * @endcode
 */
struct option_ex
{
    const char *name; /**< Long option name (without leading dashes or plus signs). */
    int has_arg;      /**< One of @c no_argument, @c required_argument,
                           or @c optional_argument. */
    int allow_plus;   /**< If 1, this option may be prefixed with @c + (short)
                           or @c ++ (long) to mean "unset / disable". */
    int *flag;        /**< If non-NULL: set to @c val on @c - prefix, cleared
                           to 0 on @c + prefix.  If NULL, @c val is returned
                           directly. */
    int val;          /**< Value to store in @c *flag (or to return). */
    int *plus_used;   /**< Optional output: set to 1 if the @c + prefix was
                           used, 0 if the @c - prefix was used.  May be NULL. */
};

/*---------------------------------------------------------------------------
 * Traditional global variables
 *---------------------------------------------------------------------------*/

/**
 * @brief Points to the argument value for the most recently parsed option
 *        that takes an argument.
 *
 * After getopt() (or any variant) returns an option character whose
 * specification in @p optstring includes @c ':', this variable points to the
 * option-argument string.  For an option with an optional argument
 * (@c "c::") that was given without one, @c optarg is @c NULL.
 */
extern char *optarg;

/**
 * @brief Index into @p argv of the next element to be processed.
 *
 * Initialised to 1 by the system.  The application may set it to 0 to
 * trigger a full re-initialisation of the internal parsing state before
 * the next call.  After all options have been parsed (getopt() returns
 * @c -1), @c optind is the index of the first non-option argument.
 */
extern int optind;

/**
 * @brief Controls whether diagnostic messages are printed to @c stderr.
 *
 * Defaults to 1 (enabled).  Set to 0 to suppress all error output from the
 * getopt functions.  Alternatively, place a leading @c ':' in @p optstring.
 */
extern int opterr;

/**
 * @brief The most recent option character that caused an error.
 *
 * When getopt() returns @c '?' (unrecognised option) or @c ':'
 * (missing argument with leading @c ':' in @p optstring), @c optopt
 * contains the option character that triggered the error.
 */
extern int optopt;

/*---------------------------------------------------------------------------
 * Standard (non-reentrant) interfaces
 *---------------------------------------------------------------------------*/

/**
 * @brief Parse short command-line options.
 *
 * Scans @p argv for the next option character listed in @p optstring.  Each
 * successive call returns the next option character, or @c -1 when all
 * options have been consumed.
 *
 * @param argc    Argument count (as passed to @c main()).
 * @param argv    Argument vector (as passed to @c main()).  Elements may be
 *                reordered in PERMUTE mode.
 * @param optstring  String of recognised option characters.  See file-level
 *                   documentation for the full syntax (leading @c '+', @c '-',
 *                   @c ':', trailing @c ':', @c '::', and @c "W;").
 *
 * @return The next option character on success.
 * @retval '?'  An unrecognised option was encountered, or a required argument
 *              is missing (when @p optstring does not begin with @c ':').
 *              @c optopt is set to the offending character.
 * @retval ':'  A required argument is missing and @p optstring begins with
 *              @c ':'.  @c optopt is set to the offending character.
 * @retval -1   All options have been parsed.  @c optind points to the first
 *              non-option argument (or equals @p argc if there are none).
 *
 * @note This function is not reentrant.  Use getopt_r() when re-entrancy is
 *       required.
 *
 * @par Example
 * @code
 * int c;
 * while ((c = getopt(argc, argv, "ab:c::")) != -1) {
 *     switch (c) {
 *     case 'a': puts("flag -a"); break;
 *     case 'b': printf("-b %s\n", optarg); break;
 *     case 'c': printf("-c %s\n", optarg ? optarg : "(none)"); break;
 *     case '?': // handle error
 *         break;
 *     }
 * }
 * @endcode
 */
MGSH_API int getopt(int argc, char *const argv[], const char *optstring);

/**
 * @brief Parse short and long command-line options.
 *
 * Behaves like getopt() but also recognises long options of the form
 * @c --name or @c --name=value.  Long options are described by the
 * @p longopts array.
 *
 * When a long option is matched:
 *   - If its @c flag field is non-NULL, @c *flag is set to @c val and 0 is
 *     returned.
 *   - Otherwise @c val is returned directly.
 *   - If @p longind is non-NULL, @c *longind is set to the index of the
 *     matched entry in @p longopts.
 *
 * @param argc       Argument count.
 * @param argv       Argument vector.
 * @param optstring  Short-option specification string.
 * @param longopts   Array of @c struct @c option, terminated by an all-zero
 *                   entry.
 * @param longind    If non-NULL, receives the index of the matched long
 *                   option.  May be @c NULL.
 *
 * @return Same as getopt(), plus 0 when a long option with a non-NULL @c flag
 *         is matched.
 *
 * @note Not reentrant.  Use getopt_long_r() for a reentrant version.
 */
MGSH_API int getopt_long(int argc, char *const argv[], const char *optstring,
                   const struct option *longopts,
                    int *longind);

/**
 * @brief Parse long options, allowing unambiguous abbreviations and single-dash
 *        long forms.
 *
 * Like getopt_long(), but a long option may also be introduced with a single
 * @c '-' (e.g. @c -verbose).  If an argument beginning with @c '-' (but not
 * @c '--') does not match any long option, it is interpreted as a cluster of
 * short options.
 *
 * @param argc       Argument count.
 * @param argv       Argument vector.
 * @param optstring  Short-option specification string.
 * @param longopts   Array of @c struct @c option, terminated by an all-zero
 *                   entry.
 * @param longind    If non-NULL, receives the index of the matched long
 *                   option.  May be @c NULL.
 *
 * @return Same as getopt_long().
 *
 * @note Not reentrant.  Use getopt_long_only_r() for a reentrant version.
 */
MGSH_API int getopt_long_only(int argc, char *const argv[], const char *optstring,
                        const struct option *longopts, int *longind);

/*---------------------------------------------------------------------------
 * Plus-prefix-aware interfaces (non-reentrant)
 *---------------------------------------------------------------------------*/

/**
 * @brief Parse short and long options with +prefix (unset) support.
 *
 * Behaves like getopt_long() but uses @c struct @c option_ex, which adds
 * support for a @c + prefix on options.  In POSIX shell convention, @c -x
 * enables (sets) an option while @c +x disables (unsets) it.  Similarly,
 * @c ++name disables a long option.
 *
 * When a @c + prefixed option is matched:
 *   - If the option's @c allow_plus is 0, an error is reported and @c '?' is
 *     returned.
 *   - If @c flag is non-NULL, @c *flag is cleared to 0 and 0 is returned.
 *   - If @c plus_used is non-NULL, @c *plus_used is set to 1.
 *
 * When a normal @c - prefixed option is matched:
 *   - Behaviour is the same as getopt_long(), but @c *flag is set to 1
 *     (rather than to @c val) when @c flag is non-NULL.
 *   - If @c plus_used is non-NULL, @c *plus_used is set to 0.
 *
 * @param argc       Argument count.
 * @param argv       Argument vector.
 * @param optstring  Short-option specification string.
 * @param longopts   Array of @c struct @c option_ex, terminated by an entry
 *                   with @c val == 0 and @c flag == NULL.
 * @param longind    If non-NULL, receives the index of the matched long
 *                   option.  May be @c NULL.
 *
 * @return Same as getopt_long(), plus @c '?' for disallowed @c + usage.
 *
 * @note Not reentrant.  Use getopt_long_plus_r() for a reentrant version.
 *
 * @par Example (shell-like set builtin)
 * @code
 * int errexit = 0, nounset = 0;
 * static struct option_ex opts[] = {
 *     { "errexit", no_argument, 1, &errexit, 'e', NULL },
 *     { "nounset", no_argument, 1, &nounset, 'u', NULL },
 *     { NULL,      0,           0, NULL,      0,  NULL }
 * };
 * int c;
 * while ((c = getopt_long_plus(argc, argv, "eu", opts, NULL)) != -1) {
 *     // -e  -> errexit = 1    +e  -> errexit = 0
 *     // -u  -> nounset = 1    +u  -> nounset = 0
 * }
 * @endcode
 */
MGSH_API int getopt_long_plus(int argc, char *const argv[], const char *optstring,
                        const struct option_ex *longopts, int *longind);

/**
 * @brief Parse long options with +prefix support and single-dash long forms.
 *
 * Combines the behaviour of getopt_long_only() and getopt_long_plus(): long
 * options may be introduced with a single @c '-' or @c '+', and the @c +
 * prefix is interpreted as "unset".
 *
 * @param argc       Argument count.
 * @param argv       Argument vector.
 * @param optstring  Short-option specification string.
 * @param longopts   Array of @c struct @c option_ex, terminated by an entry
 *                   with @c val == 0 and @c flag == NULL.
 * @param longind    If non-NULL, receives the index of the matched long
 *                   option.  May be @c NULL.
 *
 * @return Same as getopt_long_plus().
 *
 * @note Not reentrant.  Use getopt_long_only_plus_r() for a reentrant
 *       version.
 */
MGSH_API int getopt_long_only_plus(int argc, char *const argv[], const char *optstring,
                             const struct option_ex *longopts, int *longind);

/*---------------------------------------------------------------------------
 * Reentrant variants
 *---------------------------------------------------------------------------*/

/**
 * @brief Ordering mode for argument permutation in reentrant getopt.
 *
 * Controls how non-option arguments are handled relative to options:
 *   - @c REQUIRE_ORDER — stop processing at the first non-option argument
 *     (POSIX-conforming behaviour; also selected by a leading @c '+' in
 *     @p optstring).
 *   - @c PERMUTE — reorder @p argv so that all options come before all
 *     non-options (the GNU default).
 *   - @c RETURN_IN_ORDER — return non-option arguments as if they were
 *     arguments to option character @c \\1 (selected by a leading @c '-' in
 *     @p optstring).
 */
typedef enum
{
    REQUIRE_ORDER,
    PERMUTE,
    RETURN_IN_ORDER
} getopt_ordering_t;

/**
 * @brief Per-invocation state for the reentrant getopt functions.
 *
 * Zero-initialise this structure before the first call, then pass it to
 * every subsequent call in the same parsing session.  Fields marked
 * "private" should not be read or written by application code.
 *
 * @par Example
 * @code
 * struct getopt_state st = { .optind = 1, .opterr = 1, .optopt = '?' };
 * int c;
 * while ((c = getopt_r(argc, argv, "ab:", &st)) != -1) {
 *     switch (c) {
 *     case 'a': ... break;
 *     case 'b': printf("arg = %s\n", st.optarg); break;
 *     }
 * }
 * // Remaining args start at argv[st.optind]
 * @endcode
 */
struct getopt_state
{
    int optind;      /**< Index of the next argv element to process.
                          Initialise to 1 (or 0 to force full reinit). */
    int opterr;      /**< If non-zero, print diagnostics to stderr. */
    int optopt;      /**< Character that caused the most recent error. */
    char *optarg;    /**< Points to the argument of the current option,
                          or NULL if the option has no argument. */
    int initialized; /**< @private Non-zero after first call. */
    char *__nextchar;       /**< @private Pointer within current argv cluster. */
    getopt_ordering_t ordering; /**< @private Current ordering mode. */
    int first_nonopt;       /**< @private Start of non-option span. */
    int last_nonopt;        /**< @private End of non-option span. */
    int opt_plus_prefix;    /**< @private 1 if the current option used a
                                 @c + prefix. */
    int print_errors;       /**< @private Derived from @c opterr and leading
                                 @c ':' in optstring. */

    /**
     * @brief POSIX shell extension: treat a lone @c '-' as an option
     *        terminator.
     *
     * When set to 1, a bare @c '-' argument terminates option processing
     * (like @c '--') but is itself consumed rather than left as an operand.
     * This matches POSIX.1-2024 shell behaviour where "a single hyphen-minus
     * shall be treated as the first operand and then ignored."
     *
     * Defaults to 0 (standard behaviour where @c '-' is a normal operand).
     */
    int posix_hyphen;

    char **__getopt_external_argv; /**< @private Copy of argv for PERMUTE
                                        reordering. */
};

/**
 * @brief Reentrant version of getopt().
 *
 * Equivalent to getopt() but uses @p state instead of global variables.
 * Option results are read from @c state->optarg, @c state->optind, etc.
 *
 * @param argc      Argument count.
 * @param argv      Argument vector.
 * @param optstring Short-option specification string.
 * @param state     Parsing state.  Must be initialised before the first call
 *                  (see @c struct @c getopt_state).
 *
 * @return Same as getopt().
 * @retval -1 If @p state is NULL.
 */
MGSH_API int getopt_r(int argc, char *const argv[], const char *optstring,
             struct getopt_state *state);

/**
 * @brief Reentrant version of getopt_long().
 *
 * @param argc      Argument count.
 * @param argv      Argument vector.
 * @param optstring Short-option specification string.
 * @param longopts  Array of @c struct @c option (NULL-terminated).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long().
 * @retval -1 If @p state is NULL.
 */
MGSH_API int getopt_long_r(int argc, char *const argv[], const char *optstring,
                  const struct option *longopts, int *longind,
                  struct getopt_state *state);

/**
 * @brief Reentrant version of getopt_long_only().
 *
 * @param argc      Argument count.
 * @param argv      Argument vector.
 * @param optstring Short-option specification string.
 * @param longopts  Array of @c struct @c option (NULL-terminated).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long_only().
 * @retval -1 If @p state is NULL.
 */
MGSH_API int getopt_long_only_r(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longind, struct getopt_state *state);

/**
 * @brief Reentrant version of getopt_long_plus().
 *
 * @param argc      Argument count.
 * @param argv      Argument vector.
 * @param optstring Short-option specification string.
 * @param longopts  Array of @c struct @c option_ex (terminated by an entry
 *                  with @c val == 0 and @c flag == NULL).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long_plus().
 * @retval -1 If @p state is NULL.
 */
MGSH_API int getopt_long_plus_r(int argc, char *const argv[], const char *optstring,
                       const struct option_ex *longopts, int *longind, struct getopt_state *state);

/**
 * @brief Reentrant version of getopt_long_only_plus().
 *
 * @param argc      Argument count.
 * @param argv      Argument vector.
 * @param optstring Short-option specification string.
 * @param longopts  Array of @c struct @c option_ex (terminated by an entry
 *                  with @c val == 0 and @c flag == NULL).
 * @param longind   If non-NULL, receives the matched long-option index.
 * @param state     Parsing state.
 *
 * @return Same as getopt_long_only_plus().
 * @retval -1 If @p state is NULL.
 */
MGSH_API int getopt_long_only_plus_r(int argc, char *const argv[], const char *optstring,
                            const struct option_ex *longopts, int *longind, struct getopt_state *state);

/**
 * @brief Internal core implementation — not for direct use.
 *
 * All public getopt functions ultimately delegate to this function.
 * Application code should use one of the typed public wrappers instead.
 *
 * @param argc            Argument count.
 * @param argv            Argument vector.
 * @param optstring       Short-option specification string.
 * @param longopts        Pointer to a long-option array (@c struct @c option
 *                        or @c struct @c option_ex), or NULL for short-only
 *                        parsing.
 * @param longind         Output index for matched long option, or NULL.
 * @param long_only       If 1, single-dash arguments are tried as long options
 *                        first (getopt_long_only behaviour).
 * @param posixly_correct If 1, force REQUIRE_ORDER even without a leading
 *                        @c '+' in @p optstring.
 * @param state           Reentrant parsing state.
 * @param plus_aware      If 1, @p longopts is interpreted as
 *                        @c struct @c option_ex and @c + prefixes are
 *                        recognised.
 *
 * @return Same as the calling public function.
 */
MGSH_LOCAL int _getopt_internal_r(int argc, char *const argv[], const char *optstring, const void *longopts,
                       int *longind, int long_only, int posixly_correct, struct getopt_state *state,
                       int plus_aware);

/*---------------------------------------------------------------------------
 * State reset functions
 *---------------------------------------------------------------------------*/

/**
 * @brief Reset all global getopt state for a fresh parsing pass.
 *
 * Resets @c optind to 1, @c optarg to NULL, @c optopt to @c '?', and clears
 * the internal state used by the non-reentrant standard functions (getopt(),
 * getopt_long(), getopt_long_only()).
 *
 * Call this before re-scanning @p argv or before parsing a different command
 * line with the non-reentrant API.
 */
MGSH_API void getopt_reset(void);

/**
 * @brief Reset global state for a fresh plus-aware parsing pass.
 *
 * Resets @c optind to 1, @c optarg to NULL, and @c optopt to @c '?'.
 * Intended for use before re-scanning with the plus-aware non-reentrant
 * functions (getopt_long_plus(), getopt_long_only_plus()).
 *
 * @note For reentrant usage, simply zero-initialise a new
 *       @c struct @c getopt_state instead of calling this function.
 */
MGSH_API void getopt_reset_plus(void);

/*---------------------------------------------------------------------------
 * String-based wrappers
 *---------------------------------------------------------------------------*/

/*
 * String-based wrapper for getopt()
 *
 * @param argv The argument list as a strlist_t
 * @param optstring The option string as a string_t
 * @return Same as getopt(): option character, -1 for end, '?' for error
 */
MGSH_API int getopt_string(const strlist_t *argv, const string_t *optstring);

/**
 * String-based wrapper for getopt_long_plus()
 *
 * @param argv The argument list as a strlist_t
 * @param optstring The option string as a string_t
 * @param longopts Long options array (NULL-terminated)
 * @param longind Pointer to store index of long option (can be NULL)
 * @return Same as getopt_long_plus(): option character, -1 for end, '?' for error
 */
MGSH_API int getopt_long_plus_string(const strlist_t *argv,
                            const string_t *optstring,
                            const struct option_ex *longopts, int *longind);

MGSH_EXTERN_C_END

#endif /* MGSH_GETOPT_H */
