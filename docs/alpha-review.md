# Pre-alpha review: issue list

## Status (2026-11-13)

Work on this list is on branch `claude/funny-albattani-7jvldd`. Items are
marked **fixed** below where the branch addresses them; everything else is
still open.

- Build and tests (A): 1-6 **fixed**. All three build systems build; the 27
  unit tests pass under CMake, autotools (`make check`) and the GNUmakefile;
  the shell conformance scripts really run their assertions now.
- Core execution (B): 7-13 **fixed**; 14 **fixed** (exit status, silent
  errors, builtin diagnostics, leftover tokens); 15 **fixed**; 16 **fixed**;
  17 **partly fixed** (`test`/`[` implemented, `cd`/`pwd` registered;
  `read`, `type`, `command`, `umask`, `hash`, `ulimit` still missing);
  18 **fixed**; 19 **fixed**; 20-22 **open** (traps, SIGINT, interactive
  prompts) except the banner, which is now interactive-only.
- Crashes (C): 23-25 **fixed**; 26 open (contract aborts on user input).
- Conformance (D): 27 **fixed**; 28-36 open.
- Code health (E): 37 partly (all three build systems work and agree on
  file lists and macros; still three of them); 38-43 open.

The shell conformance suite (`test/sh`) now reports 439 passing and 61
failing assertions. The failures cluster as follows and are the next
targets:

| File | Fails | What is missing |
|------|-------|-----------------|
| 08_here_documents | 16 | parameter/command/arithmetic expansion in unquoted here-document bodies, `<<-` tab stripping cases |
| 17_positional_parameters | 17 | `${10}` and above, `shift n` edge cases, `$#` after `set --` with many words |
| 10_word_splitting | 7 | field splitting of unquoted expansions and command substitution |
| 16_alias_substitution | 6 | alias expansion in non-interactive mode (optional in POSIX, but the suite expects it) |
| 12_reserved_words | 5 | `for`/`while`/`until` recognised after a preceding command on the same line |
| 15_case_statement | 3 | empty branches, bracket-expression negation, field splitting of the case word |
| 03_backslash_escape | 2 | backslash-newline continuation inside words |
| 02, 04, 05, 11, 14 | 1 each | multiple-space preservation in `"$(...)"`, `"$@"` count, glob counts |

---


Review of `master` at commit `d7f0836` ("more faff"), POSIX configuration,
Linux/gcc 13. Everything below was reproduced on a locally built binary; file
and line references are to that commit.

Method: built with autotools (after a one-line typo fix so it would compile),
built a single static test binary with ASan/UBSan, ran the C unit tests, ran
`test/sh/run_tests.sh`, drove an interactive session through a pty, and ran
several hundred one-line conformance probes with `migash -c`.

Severity key: **P0** blocks an alpha (does not build, or basic scripts do not
run). **P1** crashes or clear POSIX violations. **P2** code health.

---

## A. Build, CI and tests (P0)

1. **HEAD does not compile.** `src/builtins.c:1778-1779` references
   `MIGA_FUNC_STATUS_NAME_TOO_LONG` (not in the enum), has a typo
   `MIGA_FUNC_STATUS_INVALID_NAMEs`, and is missing a closing `)`.
   Every build system fails on this file.

2. **CMake build (what CI runs) is broken.** `CMakeLists.txt` still lists
   `src/getopt.h`, `src/exec.h` and `src/exec_types_public.h`, which were moved
   to `include/miga/` or deleted. Configure fails before compiling anything.
   GitHub Actions on `master` has been red for every run since at least run 133
   (2026-03-14). The README badge shows this.

3. **GNUmakefile is stale.** It lists `src/getopt_string.c` and
   `src/exec_parse_session.c`, which do not exist, and its test lists do not
   match `test/mgsh/`.

4. **Autotools build cannot link the `migash` executable against the shared
   library.** `src/shell.c` (part of the executable, not the library) calls
   `xmalloc`/`xrealloc` in its `read_line` helper, and those symbols are hidden
   (`-fvisibility=hidden`, no `MIGA_API`). The unused `read_line` is already
   marked `FIXME: get rid of this`. Autotools also has no `make check`.

5. **C unit tests do not build against the current API.** All of them include
   `xalloc.h` with the old `src/` path. After fixing the include path, seven
   still fail to compile: `test_arithmetic_ctest`, `test_parser_ctest` (type
   `exec_t`), `test_fd_table_ctest` (`FD_REDIRECTED`), `test_func_store_ctest`
   (`ast_create_function_def` arity), `test_variable_store_ctest`
   (`variable_map_entry_t`), `test_expander_ctest` (`exec_expander.h`),
   `token_ctest` (link). Of the 22 that build: `test_lexer_arith_exp_ctest` has
   two failing cases (unclosed `((`), and `test_sig_act_ctest` segfaults
   because it passes `0x1` as a `struct sigaction *` on the POSIX build.

6. **The shell test suite passes vacuously.** `test/sh/*.sh` source
   `"$(dirname "$0")/../test_helpers.sh"`, but command substitution returns
   empty (item 12), so the helper is never loaded, every `assert_*` is
   "command not found", and the shell exits 0 anyway (item 14). The runner
   reports 14/17 passed; in reality nothing was asserted. Three tests abort the
   shell outright.

## B. Core execution: scripts cannot run (P0)

7. **Variable assignments never persist.** `x=foo; echo $x` prints an empty
   line, in `-c`, script and interactive modes. Root cause:
   `lower_simple_command` (`src/lower.c:610`) always allocates a redirection
   list, so the "assignment-only command" branch in
   `exec_frame_execute_simple_command_impl` (`src/exec_command.c:231`,
   `if (!has_words && !redirs)`) is never taken. The assignment is applied to
   the temporary variable store, which is destroyed at `out_restore_vars`.
   Fix: test `ast_node_list_size(redirs) == 0`.

8. **Special builtins mutate a throwaway variable store.** While a simple
   command with words runs, `frame->variables` is swapped for a temporary copy
   (`src/exec_command.c:261`). `export`, `readonly`, `unset`, `set`, `getopts`,
   `eval "y=7"`, `cd` (PWD/OLDPWD) and `${x=default}` all write to that copy.
   Observed: `unset PATH; echo "$PATH"` still prints PATH;
   `getopts "ab:" o -a` leaves `$o` empty; `echo ${x=default}; echo $x` prints
   `default` then nothing.

9. **Simple commands do not report an exit status in their result.**
   `src/exec_command.c:706` returns `{.status = status}` with
   `has_exit_status = false`. `if`, `while` and `until` therefore see "no
   status" and treat every condition as false:
   `if true; then echo yes; else echo no; fi` prints `no`. `$?` and `&&`/`||`
   happen to work because they read `frame->last_exit_status` directly.

10. **Control flow is dropped by command lists.**
    `exec_frame_execute_compound_list` (`src/exec_frame.c:1545-1690`) never
    copies `cmd_result.flow` into its result. Consequences:
    `f() { echo a; return 3; echo b; }; f; echo $?` prints `a`, `b`, `0`;
    `g() { exit 6; }; g; echo x` prints `x`; `{ echo a; exit 2; echo b; }`
    prints both; `break` in a `for` body does not stop the loop.

11. **The shell closes its own stdin at startup, so every pipeline fails.**
    `exec_params_t` is zero-initialised with designated initialisers, so
    `stdin_pipe_fd == 0` and `stdout_pipe_fd == 0` look like "pipe present"
    to `execute_frame_body` (`src/exec_frame.c:882-903`), which then does
    `dup2(0,0); close(0)`. strace confirms this on the first frame push. The
    next `pipe()` returns fd 0, which the child then closes. Every
    `a | b` prints `Bad file descriptor`; `read` from stdin is impossible.
    Fix: initialise both fields to -1 wherever `exec_params_t` is built, or
    use explicit flags.

12. **Command substitution is always empty.** The lexer stores the command
    text in `part->text` (`src/lexer_cmd_subst.c:53` and `:138`); the
    expander reads `part->nested` (`src/exec_frame_expander.c:950-965`), which
    is never populated. `echo "$(echo hi)"` and backticks print nothing.
    Separately, `expand_command_subst` uses `popen`, so when it is fixed the
    substituted command will run in `/bin/sh`, without migash functions,
    aliases, unexported variables or builtins, and `record_subst_status`
    stores the raw `pclose` wait status rather than `WEXITSTATUS`. It needs a
    real fork-and-run-subshell implementation.

13. **`while` and `until` loops abort the shell.** `while false; do :; done`
    dies with `Contract violation at exec_frame_execute_iteration_loop:2204 -
    params->loop_var_name is NULL`. The while clause is dispatched into the
    `for` iteration path (`execute_frame_body` tests `params->body` first, and
    the while params set `body`). `for` has its own problem: `for i in a b; do
    echo "[$i]"; done` runs once and prints `[]`.

14. **Process exit status is wrong and errors are swallowed.**
    - `main` returns the `sh_status_t` enum (`SH_OK` = 0), not the last
      command's status. `migash -c false` exits 0; `migash -c nonexistent`
      exits 0; a script's final status is lost. Only `exit N` works.
    - A runtime error stops execution silently and the shell still exits 0:
      `readonly r=1; r=2; echo after` prints nothing;
      `exec 3>/dev/null; echo x >&3; echo ok` prints nothing.
    - Builtin diagnostics set with `frame_set_error_printf` are never printed:
      `exit abc`, `return 1` at top level, `break 0`, `[` usage errors are all
      silent.
    - A syntax error inside a script file is skipped and execution continues
      (`if then fi` in a file: prints "after", exit 0). A stray `)` is
      dropped silently: `echo a; ) ; echo b` prints `a` only, exit 0.
    - `set -e` is a no-op: `builtin_set` writes `frame->opt_flags`, but the
      check at `src/exec.c:2031` reads `executor->opt.errexit`. `set -u`,
      `set -x` and `$-` are likewise never consulted (`$-` always empty).
    - `${x?msg}`, `$((1/0))`, `. missing-file` print a message but continue,
      and the shell exits 0. POSIX requires a non-interactive shell to exit.

15. **Any word shaped like `name=value` is treated as an assignment, even
    after the command name.** `try_promote_to_assignment` (`src/lexer.c:522`)
    runs on every word and the parser accepts `ASSIGNMENT_WORD` anywhere.
    `echo a=b c=d` prints an empty line; `export FOO=bar` is executed as
    `FOO=bar export` and dumps the whole environment. POSIX token rule 7(b)
    limits this to words before the command name.

16. **`cd` and `pwd` are missing from the POSIX build.** They are registered
    only inside `#ifdef MIGA_UCRT_API` (`src/builtin_store.c:430-436`).
    `cd /` gives `cd: command not found`; `pwd` works only because `/bin/pwd`
    exists.

17. **Other required builtins are missing.** `read`, `type`, `command`,
    `test`, `umask`, `hash`, `ulimit` (and optional `fc`, `newgrp`) are not
    implemented. `test` currently resolves to `/usr/bin/test` while `[` is a
    builtin, so the two behave differently. `[` handles only 0-3 operands and
    only `-t`, `-z`, `-n` as unary operators: `[ -d / ]` reports
    `too many arguments`; `[ ! -f x ]` returns 2; no `-a`/`-o`, no file tests.

18. **Stdio is not flushed before redirections are restored.** Builtins print
    with `printf` to `stdout`; `exec_redirect.c` flushes before applying
    (line 741) but the POSIX restore path does not. Output written while fd 1
    pointed at a file is flushed later, into whatever fd 1 points at then:
    `echo hi >f 2>&1; cat f` prints nothing, and a subsequent
    `{ echo a; echo b; } >g; cat g` printed `a b hi`.

19. **`exec` with only redirections does not persist them.** The special
    builtin path applies the redirections, calls `builtin_exec` (which
    returns 0 as a no-op for argc==1), then restores them
    (`src/exec_command.c:383-396`). `exec 3>/dev/null` has no effect.

20. **Traps never run.** `trap_handler` calls `execute_trap_action`, which is
    a stub that prints `Executing trap action for signal N: ...` and returns
    (`src/trap_store.c:239-249`). The EXIT trap likewise prints
    `Executing EXIT trap action: ...` without executing anything. The handler
    also does `printf` and store lookups directly in signal context (not
    async-signal-safe), while the executor has a separate `trap_pending[]`
    mechanism that nothing sets. `trap - EXIT; trap` aborts with
    `trap_store_run_exit_trap:961 - store->exit_action is NULL`.

21. **Interactive mode: Ctrl-C kills the shell.** No SIGINT handling is
    installed (`sig_act_store_install_default_signal_handlers` is commented
    out at `src/exec.c:998`); a pty session ended with signal 2 on `^C`.
    Also, when stdin is not a tty, `shell_execute` still calls
    `exec_setup_interactive` for `SHELL_MODE_STDIN` (`src/shell.c:138`), so
    `echo 'echo hi' | migash` prints `$ ` prompts into the output.

22. **Banner and farewell on stdout in every mode.** `Welcome to Miga Shell...`
    and `Goodbye!` are printed by `main` even for `-c` and script files. This
    corrupts `$(migash -c ...)`, breaks use as a `#!` interpreter, and should
    be interactive-only (or stderr, or removed). Also `log_init` is never
    called (`main.c` FIXME), so `LOG_LEVEL`/`LOG_ABORT_LEVEL` have no effect.

## C. Crashes and memory safety (P1)

23. **Double free at exit whenever positional arguments are given.**
    `main.c:528` calls `free(arg_array)` after `miga_arena_end()` has already
    freed every arena allocation (ASan: freed in `arena_reset_ex`). Every
    `migash script.sh arg` and `migash -c '...' name arg` aborts with
    `double free or corruption (fasttop)` after running.

24. **`exec_destroy` destroys a NULL string.** `src/exec.c:159`
    (`shell_name` or `error_msg`) triggers
    `[ERROR] Precondition failed at string_destroy:249 - *str is NULL` on
    stderr at every exit.

25. **Parse-error paths corrupt memory.** `echo a; fi` aborts in
    `arena_xfree` via `token_list_destroy` at `exec_frame_string_core`
    (`src/exec_frame.c:2814`). Test `17_positional_parameters.sh` aborts with
    `ast_node_list_clone:527 - other is NULL`. A here-document whose delimiter
    is the last line of the input with no trailing newline aborts with
    `lexer_advance:387 - lx->pos >= string_length` (the EOF branch in
    `lexer_heredoc.c:85-89` advances and falls through instead of returning
    true).

26. **`Expects_*` contracts abort on user input.** Several of the aborts above
    are contract macros reacting to ordinary scripts. Anything reachable from
    script input should be a diagnosed error, not `abort()`.

## D. POSIX conformance gaps (P1)

27. **Arithmetic expansion gives wrong answers for many operators.**
    Observed: `$((1<<4))`=4, `$((16>>2))`=2, `$((5&3))`=53, `$((5|3))`=53,
    `$(((1+2)*3))`=7, `$((3>2))`=2, `$((0||1))`=10. Parentheses, shifts,
    bitwise ops, relational ops and `||` are all mishandled in
    `parse_expression` (`src/arithmetic.c:396`); tokenisation looks right, so
    the bug is in operand/precedence handling. Division by zero prints
    `[ERROR] ...` and yields 0 instead of failing the command.

28. **Tilde expansion is not implemented.** `echo ~` prints `~`. The expander
    has `PART_TILDE`, but no lexer path produces it.

29. **`${10}` is rejected** (`bad substitution: unexpected character '0'`).

30. **Background jobs and special parameters.** `$!` is 0 after `cmd &` in a
    list; `jobs` prints nothing for a running background job; `$PPID` and
    `$LINENO` are empty.

31. **Redirection edge cases.** `echo x >&9` (fd 9 closed) silently succeeds
    with status 0; `cat <&0 </etc/hostname` hangs instead of reading the file
    (a second redirection of fd 0 is ignored).

32. **`alias` listing prints nothing** (`alias x=echo; alias`). Alias
    expansion is not applied in non-interactive mode, which is permitted, but
    the listing should still print.

33. **`kill -l` output is out of order with gaps** (8 before 7, 31 listed
    after 6). `kill -TERM $$` with a TERM trap set does not run the trap (see
    item 20).

34. **`case` fall-through `;&` is a parse error.** POSIX.1-2024 added `;&`.
    Low priority.

35. **`echo` accepts `-n`/`-e`/`-E` and prints "unrecognized option" for other
    dashes.** POSIX `echo` takes no options. Decide and document; low
    priority.

36. **No `--help` / `--version`.** Both produce "unrecognized option" and the
    usage text. Nice to have for an alpha.

## E. Code health (P2)

37. **Three build systems that disagree.** CMake (used by CI and
    `docs/build_guide.md`), GNUmakefile (Fedora-only, stale) and autotools
    (newest, no tests). Pick one as primary, make CI use it, and make the
    other two either track it or go away. README has no build instructions.

38. **Duplicated state.** Options live in both `executor->opt` and
    `frame->opt_flags`; the exit status lives in both
    `executor->last_exit_status` and `frame->last_exit_status`; variables in
    `executor->variables` and `frame->variables`. Items 9 and 14 are direct
    results. One owner per piece of state.

39. **Dead and stub code.** `shell_feed_line`, `shell_run_script` are
    `TODO` stubs returning `SH_OK`; `read_line` in `shell.c` is unused and
    causes item 4; `-Wall` flags unused statics `remove_quotes`,
    `is_parameter_set`, `is_ifs_char`, `chdir_stub`, unused label
    `out_cleanup_words`, and `-Wswitch` warnings for unhandled
    `MIGA_EXEC_STATUS_EXIT`/`NOT_IMPL` in `shell.c`. Ten files redefine
    `_POSIX_C_SOURCE` after `config.h`.

40. **Naming leftovers.** CMake targets are `sh23base`/`sh23store`/`sh23logic`;
    GNUmakefile and coverage title say `mgsh`; `docs/index.md` says "the sh23
    shell"; tests live in `test/mgsh/`. The version `0.0.1` exists only in
    `configure.ac` and is not exposed to the binary.

41. **Public API surface.** `libmigash.h` exports the allocator and mutex
    headers; `shell.c` depends on hidden symbols. Decide what is public and
    annotate it, or move `shell.c` into the library.

42. **Test wiring.** No build system runs `test/sh/run_tests.sh`; the runner
    hard-codes `SCRIPT_DIR='.'`; the tests should fail loudly when the helper
    cannot be sourced instead of passing on "command not found".

43. **Repository hygiene.** `.vscode/`, `.dir-locals.el` and the one-off
    `tools/add_config_h.py` are committed. CI only runs on pushes to
    `master`, so branch pushes get no signal.

---

## What works

For balance, these behaved correctly in the probes: single/double quoting and
backslash escapes, string concatenation, pathname expansion including `?`,
`[...]` and dot-files, `case` with alternation and bracket patterns, parameter
expansion modifiers (`:-`, `-`, `:+`, `:?`, `#`, `##`, `%`, `%%`, `${#x}`),
here-documents with and without `-` and quoting (given a trailing newline),
function definition and call with arguments, `&&`/`||` chains, subshells and
brace groups with redirections, `>`/`>>`/`<`/`2>&1`/`>&-`/noclobber,
background `&` plus `wait`, `printf` including `%b`, `basename`/`dirname`,
`shift`, `set --`, `"$@"` vs `"$*"`, and the ISO C configuration compiles and
runs `echo`.

## Suggested order of attack

1. Items 1-4 so the tree builds in CI again, then 5-6 so tests mean something.
2. Items 7, 9, 10, 11, 12, 13: each is a small, well-localised fix, and
   together they make ordinary scripts run.
3. Items 14, 15, 16, 18, 23, 24: exit status, assignment-word rule, `cd`,
   flushing, and the two exit-time memory bugs.
4. Items 17, 20, 21, 22, 27: missing builtins, traps, signals, banner,
   arithmetic.
5. Everything else.
