# POSIX Shell Quoting Test Suite

A comprehensive test suite for verifying POSIX shell quoting implementation compliance with POSIX.1-2024 Section 2.2.

## Overview

This test suite validates that a shell correctly implements the three quoting mechanisms defined by POSIX:

1. **Escape Character (Backslash)** - `\`
2. **Single-Quotes** - `'...'`
3. **Double-Quotes** - `"..."`

## Quick Start

```bash
# Run tests with the default /bin/sh
./run_tests.sh

# Run tests with a specific shell
./run_tests.sh /bin/dash
./run_tests.sh /bin/bash
./run_tests.sh /bin/bash --posix
./run_tests.sh /usr/local/bin/zsh
```

## Test Files

| File | Description |
|------|-------------|
| `01_single_quotes.sh` | Single quotes preserve literal value of all characters |
| `02_double_quotes.sh` | Double quotes allow `$`, `` ` ``, `\` to retain special meaning |
| `03_backslash_escape.sh` | Backslash escapes the following character |
| `04_unquoted_special.sh` | Verifies special characters work when NOT quoted |
| `05_parameter_expansion.sh` | Quoting interaction with `$var`, `${var}`, etc. |
| `06_command_substitution.sh` | Quoting interaction with `$(...)` and `` `...` `` |
| `07_arithmetic_expansion.sh` | Quoting interaction with `$((...))` |
| `08_here_documents.sh` | Quoting controls here-document expansion |
| `09_quote_concatenation.sh` | Adjacent quoted strings combine correctly |
| `10_word_splitting.sh` | Quoting prevents field splitting |
| `11_pathname_expansion.sh` | Quoting prevents glob expansion |
| `12_reserved_words.sh` | Quoting prevents reserved word recognition |
| `13_edge_cases.sh` | Corner cases and potential pitfalls |
| `14_conditional_special.sh` | Characters special in certain contexts |
| `15_case_statement.sh` | `case` patterns and matching |
| `16_alias_substitution.sh` | Alias substitution (POSIX 2.3.1) |
| `17_positional_parameters.sh` | Positional and special parameters |
| `18_traps_signals.sh` | `trap`, the EXIT trap and SIGINT (POSIX 2.11); needs `TEST_SHELL` for child shells |
| `19_set_options.sh` | `set -e -u -f -C -n -x -v -a`, `set -o`/`+o`, `$-`, and `wait` without operands; needs `TEST_SHELL` |
| `20_read_builtin.sh` | the `read` utility: IFS splitting, `-r`, line continuation, status at end of file, one line per call |
| `21_umask.sh` | the `umask` utility: octal and symbolic (`-S`) output, octal and symbolic operands, effect on created files (POSIX configuration) |
| `22_command_type.sh` | `command` (bypassing functions, special built-ins as regular, `-p`, `-v`, `-V`) and `type` |
| `23_hash_ulimit.sh` | `hash` (statuses; the shell keeps no table) and `ulimit -f` (POSIX configuration) |

## Expected failures

`expected_failures.txt` lists test files that the shell under development is
known to fail. When the shell under test is migash, a failure of a listed
file is reported as `XFAIL` and does not fail the run, and a pass is reported
as `XPASS` as a reminder to remove the entry (`MIGASH_STRICT_XFAIL=1` makes
an XPASS fail the run). The list is ignored for reference shells such as
dash or bash, so `./run_tests.sh dash` still checks that the tests
themselves are right.

## Characters That Must Be Quoted

Per POSIX.1-2024, these characters must be quoted to represent themselves:

| Character | Name | Special Meaning |
|-----------|------|-----------------|
| `\|` | Pipe | Pipeline |
| `&` | Ampersand | Background/AND |
| `;` | Semicolon | Command separator |
| `<` | Less-than | Input redirection |
| `>` | Greater-than | Output redirection |
| `(` | Open paren | Subshell |
| `)` | Close paren | Subshell |
| `$` | Dollar | Parameter expansion |
| `` ` `` | Backtick | Command substitution |
| `\` | Backslash | Escape character |
| `"` | Double quote | Quoting |
| `'` | Single quote | Quoting |
| `<space>` | Space | Word splitting |
| `<tab>` | Tab | Word splitting |
| `<newline>` | Newline | Command terminator |

## Conditionally Special Characters

These may need quoting depending on context:

| Character | Context Where Special |
|-----------|----------------------|
| `*` | Pathname expansion (glob) |
| `?` | Pathname expansion (single char) |
| `[` `]` | Pathname expansion (character class) |
| `^` | Bracket negation `[^...]` |
| `-` | Bracket range `[a-z]` |
| `!` | Bracket negation `[!...]`, pipeline negation |
| `#` | Comment (at start of word) |
| `~` | Tilde expansion (at start of word) |
| `=` | Assignment |
| `%` | Parameter expansion `${var%pattern}` |
| `{` `}` | Brace expansion, command grouping |
| `,` | Brace expansion `{a,b,c}` |

## Quoting Rules Summary

### Single Quotes (`'...'`)
- Preserves literal value of ALL enclosed characters
- Cannot include a single quote (even escaped)
- No expansions occur

### Double Quotes (`"..."`)
- Preserves literal value of most characters
- **Exceptions**: `$`, `` ` ``, `\`, and `!` retain special meaning
- `\` only escapes: `$`, `` ` ``, `"`, `\`, and newline
- Prevents word splitting and pathname expansion

### Backslash (`\`)
- Escapes the next character (preserves literal value)
- Exception: `\<newline>` is line continuation (both removed)
- Inside double quotes, only escapes: `$`, `` ` ``, `"`, `\`, newline

## Writing Additional Tests

Use the provided `test_helpers.sh`:

```bash
#!/bin/sh
. "$(dirname "$0")/../test_helpers.sh"

printf "  Testing my feature...\n"

# Assert two values are equal
assert_eq "expected" "actual" "description"

# Assert command output
assert_output "expected" "description" command arg1 arg2

# Assert argument count
assert_argc 3 "description" "$@"

# Must call at end
finish_tests
```

## Exit Codes

- `0` - All tests passed
- `1` - One or more tests failed

## Compatibility Notes

- Tests are written for POSIX compliance
- Some tests note behavior that varies between shells
- Brace expansion `{a,b,c}` is NOT required by POSIX
- The `[[` keyword is NOT POSIX (not tested)

## References

- [POSIX.1-2024 Section 2.2 - Quoting](https://pubs.opengroup.org/onlinepubs/9799919799/)
- [Shell Command Language](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html)
