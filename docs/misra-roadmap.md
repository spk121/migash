# MISRA C roadmap (misra-c branch)

This branch turns migash into a MISRA C compliant shell. It is not meant to
be merged back into master; it will become its own project.

## Decisions

| Topic | Decision |
|---|---|
| Platforms | ISO C only. The POSIX and UCRT code paths are removed. The target is a bare-metal board with a UART console and a flash file system (see `misra-notes.md`). |
| Standard | MISRA C:2023, against C11. C23 constructs (`#elifdef`, `-std=c2x`) are replaced. |
| Dynamic memory | Staged. First the arena allocator is backed by fixed-size static pools instead of the C library heap (Rule 21.3). Then the data structures move to fixed-capacity types until the pools disappear (Directive 4.12). |
| Memory budget | A 64–128 KB SRAM class device. Every capacity is a compile-time constant in one configuration header. |

## Measuring

- **Compliance:** cppcheck's MISRA add-on, run over the ISO C build. It
  implements MISRA C:2012 (most rule numbers carry over to 2023), so it is a
  close stand-in, not a certification tool. The violation count per rule is
  recorded in `misra-baseline.md` and should only go down.
- **Memory:** the arena statistics build (`-DENABLE_ARENA_STATS=ON`) reports
  peak usage, leaks, and a histogram of allocation sizes, which sizes the
  pools.
- **Behaviour:** the unit tests and the leak regression test, plus an ISO C
  end-to-end script suite (to be written: the existing conformance scripts
  need fork, pipes and command substitution).

## Stages

1. **ISO C only.** Remove the POSIX and UCRT code paths, the build options
   that select them, and the tests that need them.
2. **C11.** Build with `-std=c11 -pedantic-errors` and replace C23 features.
3. **Compliance baseline.** Run the MISRA checker and record the counts per
   rule in `misra-baseline.md`.
4. **Limits header.** Gather every size limit (line length, words per
   command, nesting depth, variables, functions, aliases, here-document size,
   job table) into one header of compile-time constants.
5. **Static pools.** Back the arena with fixed-size pools sized from the
   allocation histogram. No `malloc`, `calloc`, `realloc`, `free` or
   `strdup` anywhere (Rule 21.3).
6. **Error propagation.** Replace the `setjmp`/`longjmp` out-of-memory
   recovery with returned errors (Rule 21.4).
7. **Fixed-capacity types.** Convert strings, lists, tokens, the parse tree
   and the stores to fixed-capacity types until the allocator can go
   (Directive 4.12). Merge the two parse-tree representations (gnode and AST)
   into one.
8. **Bounded recursion.** Remove recursion from the parser, the lowering pass,
   the executor and expansion, or bound and document it (Rule 17.2).
9. **Standard library.** Replace stdio with the UART and flash layer
   (Rule 21.6), remove variadic functions (Rule 17.1), and remove `system()`,
   `getenv()`, `exit()` and `abort()` (Rules 21.8 and 21.21). Without
   `system()` the shell runs only its own builtins.
10. **Remaining rules.** Work through what the checker still reports, with
    documented deviations where a rule cannot be met.
