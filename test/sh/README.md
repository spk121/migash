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
| `24_arithmetic_errors.sh` | arithmetic errors (division by zero, malformed expressions) as expansion errors: diagnostic, status, a non-interactive shell exits, inside functions, conditions and pipelines |
| `25_ppid.sh` | the `PPID` variable: the parent's process ID, kept in subshells, set regardless of the environment, not exported |
| `26_lineno.sh` | the `LINENO` variable: script lines (blank lines, functions, loop bodies, a command over several lines), `-c` strings, sourced files, standard input |
| `27_jobs.sh` | the `jobs` utility in a non-interactive shell: running and finished jobs, `-l`, `-p`, job IDs (`%1`, `%%`, unknown), a reported job is forgotten |

## Interactive tests

`../interactive/sigint_pty.py <shell>` starts the shell on a pseudo-terminal
and checks the interactive SIGINT behaviour: Ctrl-C at the prompt, with a
partial line typed, during a foreground command, a loop and a subshell
(`$?` is 130 and the shell goes on), a background job surviving Ctrl-C,
a trapped INT running its action, `trap - INT` restoring the default, and
`exit` ending the shell with its status. It needs Python 3 and a POSIX
system; CMake registers it as `interactive_sigint` in the POSIX
configuration. dash passes all of it; bash fails the trapped-INT check
because it does not run an INT trap when only the foreground child was
killed by Ctrl-C.

`../interactive/prompt_pty.py <shell>` checks the interactive prompts
(POSIX 2.5.3 and 2.8.1): the default PS1 (`# ` for root) on standard
error, parameter expansion of PS1 and PS2, an empty PS1, PS2 for compound
commands, quotes, here-documents and backslash-newline, end of file at a
PS2 prompt, that shell errors (special built-in misuse, `${x?}`, a syntax
error, command not found, a redirection error, a failed `.` or `exec`) do
not end an interactive shell, `ENV`, `set -o ignoreeof`, end of file at
the prompt, and that a shell reading a pipe prints no prompt unless
started with `-i`. CMake registers it as `interactive_prompts`. dash
fails one check (it exits on a failed `exec`) and bash another (it exits
at end of file under PS2). Both scripts share `../interactive/ptyshell.py`.

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
