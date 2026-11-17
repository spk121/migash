# Developer logging

`src/logging.h` provides `log_debug()`, `log_warn()`, `log_error()` and
`log_fatal()`. They exist for the people working on the shell, never for the
people using it. A problem that a shell user should know about is reported
through the shell's own diagnostics (`frame_set_error_printf()`, or a message
on standard error with the shell's name), as POSIX describes for the shell; it
may in addition be logged, but a log line is never the only report. Help for
debugging shell *scripts* is the business of `set -x`, `set -v` and friends,
not of this module.

## Levels

- **fatal**: an internal invariant is broken and the process cannot continue
  safely. This is what the `Expects_*` contract macros use. The message is
  always printed, whatever the threshold, and the process aborts (a core dump
  is the point). Memory exhaustion is handled by the arena, not by `log_fatal`.
- **error**: the shell met a state it does not understand or a failure it
  could not recover from cleanly: the lowering sees a tree node it has no rule
  for, a descriptor is not where the bookkeeping says it is, `fork()` fails.
  Each occurrence points at a bug in the shell or at an exhausted resource and
  is worth a report. The user-visible consequence, if any, is reported
  separately through the shell's diagnostics.
- **warn**: something unexpected happened and the shell recovered on its own:
  a fallback path was taken, a consistency check found and repaired a
  mismatch, a feature a configuration cannot provide was skipped. Nothing the
  user did causes a warning; a user mistake is a shell diagnostic.
- **debug**: a trace of what the shell is doing internally: lexer and parser
  status per line, tokens, frame pushes and pops, redirections applied and
  restored with their descriptors, job-table changes, alias and environment
  handling, the parsed command line. Only meaningful with the source at hand.
  Debug output must never change behaviour, and it stays out of per-character
  loops.

## Rules for a log line

- Say which module or function speaks and what happened: `restore(posix):
  dup2(10, 1) failed: EBADF`. Include the identifiers that let the reader
  find the state: descriptors, process IDs, job numbers, line numbers,
  variable names.
- No trailing newline; the module adds it.
- Never log a user's mistake (a syntax error, an unknown command, a bad
  operand). Those go to the user, through the shell's diagnostics, and only
  there.
- Never rely on a log line being seen: the default threshold hides it.

## Configuration

Logging is configured from the environment when `main()` starts, by
`log_init()`:

- `MIGASH_LOG_LEVEL`: `debug`, `warn`, `error`, `fatal` or `none`, case does
  not matter. Messages below the level are discarded. The default is `none`
  in a release build and `warn` in a debug build (`MIGA_DEBUG_BUILD`), so a
  user of a release shell never sees developer output.
- `MIGASH_LOG_ABORT_LEVEL`: `warn`, `error`, `fatal` or `none`. A message at
  or above this level aborts the process, which puts the first unexpected
  state under the debugger. The default is `fatal`.

The variables carry the shell's name because POSIX reserves no `LOG_LEVEL`
for the shell and other programs run from it may use that name for their own
purposes. Output goes to standard error as `[LEVEL] message`.
