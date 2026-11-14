# Notes on the shell execution environment

This describes how `exec.c`, `exec_frame.c`, `exec_command.c`,
`exec_frame_expander.c` and `exec_redirect.c` fit together. It is a
developer's map, not an API reference; the public API is in
`include/miga/exec.h` and `include/miga/frame.h`.

## Two objects: the executor and the frame stack

`miga_exec_t` (one per shell) holds process-wide state: the shell's pid and
ppid, interactivity and login flags, signal bookkeeping (`trap_pending[]`,
`sigint_received`), the job table, the builtin table, the error message, the
`exit_requested` flag, the parse session, the RC-file settings, and the
pointers `top_frame` / `current_frame`.

`miga_frame_t` is one execution environment in the POSIX sense (XCU 2.12).
Every frame carries the state a shell execution environment has:

| Field | Meaning |
|-------|---------|
| `variables`, `local_variables` | shell variables (and function locals when the frame has them) |
| `positional_params` | `$0`, `$1`, ... |
| `functions`, `aliases` | function and alias tables |
| `open_fds` | the `fd_table_t` that tracks redirections and their backups |
| `traps` | trap actions |
| `opt_flags` | `set -e`, `-u`, `-x`, ... |
| `working_directory`, `umask` | cwd and file creation mask |
| `last_exit_status`, `last_bg_pid` | `$?`, `$!` |
| `pending_control_flow`, `pending_flow_depth` | a `return`, `break N`, `continue N` or `exit` that a builtin has raised and the enclosing code has not yet consumed |
| `source_line`, `source_name` | `$LINENO` and the script name |

Frames form a stack through `parent`. `exec_frame_push()` creates a frame
and makes it `executor->current_frame`; `exec_frame_pop()` runs the EXIT
trap if the policy says so, propagates the exit status to the parent if the
policy says so, releases what the frame owns, and makes the parent current.

## Frame types and policies

Every frame type has an entry in `EXEC_FRAME_POLICIES[]`
(`exec_frame_policy.h`). The policy decides, per kind of state, whether the
frame **shares** the parent's instance, gets a **copy**, or gets its **own**,
and how control flow behaves. In brief:

| Frame | Forks | Variables | Positional | `return` | `break`/`continue` |
|-------|-------|-----------|------------|----------|--------------------|
| `TOP_LEVEL` | no | own (from envp) | own (argv) | error | error |
| `SUBSHELL` (also command substitution) | yes | copy | copy | error | error |
| `BRACE_GROUP` | no | share | share | passes through | passes through |
| `FUNCTION` | no | share (plus locals) | own (call args) | **target** | error |
| `LOOP` | no | share | share | passes through | **target** |
| `TRAP` | no | share | share | error | error; `set -e` suspended |
| `BACKGROUND_JOB` | yes, new process group | copy | copy | error | error |
| `PIPELINE` / `PIPELINE_CMD` | orchestrator forks one child per command | copy | copy | error | error |
| `DOT_SCRIPT` | no | share | share, `$0` is the sourced file | **target** | error |
| `EVAL` | no | share | share | passes through | passes through |

`exec_in_frame(parent, type, params)` is the one-stop helper: it forks if the
policy says so (parent waits, or records a background job), pushes the frame,
sets up the process group, runs `execute_frame_body()`, pops the frame, and
`_exit()`s in a forked child. `execute_frame_body()` applies the frame's
redirections, then runs whichever of `params->iteration_words` (for),
`params->condition` (while/until), `params->pipeline_commands` (pipeline) or
`params->body` is set, in that order of precedence, and passes the result
through `handle_control_flow()`.

## Entry points

```
exec_create()                      -- allocate the executor
exec_setup_interactive() /         -- create the top-level frame, import
exec_setup_noninteractive()           envp, register builtins, source RC files
exec_execute_command_string()      -- run a complete -c string
exec_execute_stream()              -- the REPL: run a FILE* line by line
exec_execute_stream_once()         -- run a whole FILE* (used by ".")
exec_request_exit()                -- make the main loops stop
exec_destroy()
```

All of them end up in `exec_frame_string_core(frame, text, session)`, which
takes **one line of input**: lexer -> tokenizer -> parser -> `ast_lower()` ->
`exec_frame_execute_dispatch()`. A line that does not complete a command (an
open quote, here-document or compound command) returns
`MIGA_EXEC_STATUS_INCOMPLETE` and the session keeps the tokens for the next
line. Tokens left over after a successful parse are a syntax error, never
dropped. After executing, a `return` or `exit` that unwound to this frame is
recorded in `frame->pending_control_flow` so that whoever is reading the
stream can stop.

`exec_frame_stream_core(frame, fp, session)` reads exactly one physical line
(of any length) and feeds it to the string core; it returns
`MIGA_EXEC_STATUS_EMPTY` at end of file. The REPL loops over it so that it can
print prompts and check traps, `set -e` and exit requests between lines.
`exec_execute_stream_once()` loops over it to end of file and stops early on
an error, an exit, or a `return` from the sourced file.

Scripts and sourced files are opened with `lib_fopen_script()`, which puts
the file on a descriptor >= 10 with close-on-exec, so `exec 3>file` in the
script cannot redirect the script itself and children do not inherit it.

## Results and control flow

Every execute function returns an `exec_frame_execute_result_t`:

| Field | Meaning |
|-------|---------|
| `status` | `OK`, `ERROR` (a shell error: stop the current list), `NOT_IMPL`, `EXIT` |
| `has_exit_status`, `exit_status` | the command's `$?`; every simple command sets it |
| `flow`, `flow_depth` | `NORMAL`, `RETURN`, `BREAK`, `CONTINUE` or `TOP` (exit), with the `N` of `break N` |

Rules:

- `if`, `while`, `until`, `&&` and `||` read `exit_status` from the result,
  not from the frame.
- A compound list stops at the first command whose result carries a flow, or
  as soon as `executor->exit_requested` is set, and returns that flow upward.
  `&&`/`||` do not run their right side after a flow.
- Builtins raise control flow by setting `frame->pending_control_flow`
  (`return`, `break`, `continue`) and, for `exit` in a non-top frame, also
  `exec_request_exit()`. `exec_frame_execute_simple_command()` moves the
  pending flow from the frame into the result.
- `handle_control_flow()` applies the frame policy: a `FUNCTION` or
  `DOT_SCRIPT` frame consumes `RETURN` and turns it into a plain exit status;
  a `LOOP` frame consumes `BREAK`/`CONTINUE` with depth 1 and decrements the
  depth otherwise; transparent frames pass flows up; `TOP` always passes up.
- `exit` in the top-level frame terminates the process directly (after the
  EXIT trap); in any other frame it unwinds through `TOP` + `exit_requested`
  and the main loop returns `MIGA_EXEC_STATUS_EXIT`.

## Simple commands (`exec_command.c`)

`exec_frame_execute_simple_command_impl()` follows XCU 2.9.1:

1. **Assignment-only command** (`x=1`, no words, no redirections: the lowering
   always allocates a redirection list, so its *size* is tested): expand the
   values and assign in `frame->variables`. Status 0.
2. Otherwise expand the words and build the runtime redirections **against
   the real variable store**, so `x=1 echo $x` sees the old value of `x`.
3. Resolve the command name in POSIX order: special builtin, then function,
   then regular builtin, then external utility.
4. Apply prefix assignments according to the command kind:
   - special builtin: permanently (`apply_prefix_assignments`);
   - function or regular builtin: temporarily and exported
     (`apply_prefix_assignments_temporarily`, undone at `done_execution`);
   - external utility: not applied to the shell at all; a temporary copy of
     the store plus the assignments (`build_temp_store_for_siple_command`)
     only provides the child's environment.
5. Apply redirections, run the command, restore redirections. Builtins run in
   the current frame against the real store, so `unset`, `export`, `cd`,
   `getopts` and friends affect the shell. Diagnostics a builtin recorded
   with `frame_set_error_printf()` are printed to stderr and cleared by
   `report_builtin_error()`.
6. Assignments plus redirections with no command name (`x=1 >f`) apply and
   undo the redirections (creating the file) and keep the assignments.
7. A redirection error gives the command status 1 and prints the message; for
   a special builtin it is also a shell error (`status = ERROR`), which makes
   a non-interactive shell stop.
8. `exec` with only redirections calls `exec_redirect_commit_redirections()`
   instead of restoring, so they stay in effect.
9. Functions run through `exec_frame_execute_function_body()` in a `FUNCTION`
   frame; an `exit` inside one is propagated as `TOP`.

## Redirections (`exec_redirect.c`, POSIX path)

`exec_redirect_apply_redirectons()`:

1. Pre-pass: for every target descriptor, save a backup with
   `fcntl(F_DUPFD_CLOEXEC, 10)` and record it in the fd table as `FD_IS_SAVED`
   with its original number. A target that is not open (e.g. `exec 3>f`) gets
   `FD_IS_RESTORE_CLOSE` instead: restoring it means closing it.
2. Apply in order, left to right. For a file target, if `open()` happens to
   return the target descriptor itself nothing is duplicated or closed.
   `n>&m` parses `m` from the word (`fixed_fd` is only used for a real
   IO_NUMBER token); `n>&-` on a closed descriptor succeeds.
3. On failure, restore what was done and return `ERROR` with the message on
   the executor.

`exec_redirect_restore_redirections()` flushes `stdout`/`stderr` first (builtin
output written under a redirection must land in the redirected file), then
`dup2()`s each backup onto its original, closes the backups, and closes the
descriptors marked `FD_IS_RESTORE_CLOSE`. `exec_redirect_commit_redirections()`
closes the backups and forgets the markers instead.

IO numbers: the lexer emits `IO_NUMBER` for a digit sequence that starts a new
token and is immediately followed by `<` or `>`, anywhere on the line; if the
lexer produced a plain word the tokenizer converts that word in place.

## Expansions (`exec_frame_expander.c`)

- Parameter, arithmetic and tilde expansion and quote removal work on the
  token parts the lexer produced.
- Command substitution (`$(...)` and backquotes): the lexer stores the command
  text in `part->text`. `expand_command_subst()` forks, connects the child's
  stdout to a pipe, runs the text in the child through
  `exec_frame_string_core()` with a fresh parse session, and `_exit()`s with
  the child's `$?`. The parent reads the pipe, strips trailing newlines and
  records the status for `$?`. Because the child is this shell, functions,
  aliases, unexported variables and builtins are visible, unlike `popen(3)`.
- Arithmetic (`arithmetic.c`): the text inside `$(( ))` is expanded as if
  double-quoted (`expand_heredoc()`), then parsed; `&&`/`||` and `?:` parse
  but do not evaluate the unselected operand.

## Pipelines

`exec_frame_execute_pipeline_orchestrate()` runs in a `PIPELINE` frame in the
shell process. It creates all pipes up front, forks one child per command, and
in each child duplicates the pipe ends onto stdin/stdout, closes every pipe
descriptor, dispatches the command, and `_exit()`s with its status. The first
child becomes the process-group leader and the others join (both parent and
child call `setpgid()` to avoid the race). The parent closes its pipe ends as
it goes and waits for every child; the pipeline's status is the last
command's, negated for `! pipeline`.

## Things to keep in mind

1. **`$$` does not change in a subshell.** The pid is stored once in the
   executor; a forked child inherits it, which is what POSIX requires.
2. **Job control is disabled in subshells**, and a subshell starts with an
   empty job table.
3. **Options have one owner.** Before the top-level frame exists (while
   `main.c`/`shell.c` apply the command-line flags) they live in
   `executor->opt`; `exec_frame_push()` copies them into the top frame's
   `opt_flags`, which is the owner from then on (shared or copied by the
   frame policy). `exec_get_flag_*()`/`exec_set_flag_*()` go through
   `exec_opts()`, which picks the right copy. `set -e` is applied per
   command by `exec_frame_apply_errexit()` (after every simple command, after
   compound commands in a list, after the last command of an and-or list and
   after a pipeline); `executor->errexit_ignore_depth` is raised while
   executing the contexts in which `-e` is ignored (`if`/`while`/`until`
   conditions, the left side of `&&`/`||`, a pipeline beginning with `!`).
   `set -u` and `${var:?word}` set `executor->expansion_error`, which the
   simple-command executor turns into a shell error (status 2). `set -n`
   is checked in the dispatcher and the list loop, `set -v` in the line
   reader, `set -x` in the simple-command executor after expansion, `set -a`
   where variables are assigned, `set -C` in the file-redirection open.
   The exit status still lives in both `executor` and `frame`.
4. **Traps.** The signal handler (`trap_store.c`) only sets
   `executor->trap_pending[signo]`. `exec_run_pending_traps()` runs the
   pending actions in an `EXEC_FRAME_TRAP` frame under the current frame;
   it is called after every simple command, so an action runs after the
   command during which the signal arrived and before the next one, and
   between REPL lines. `$?` is restored after the action unless it called
   `exit`, in which case the simple command's result carries `TOP` and the
   exit status. `exec_run_exit_trap()` runs the EXIT trap once (it takes
   the action out of the store first): at the end of the shell's input, in
   `exit`, and in `exec_frame_pop()` for frames whose policy has
   `exit_trap_runs` (top level, subshells, background jobs). Subshells
   reset non-ignored traps, including EXIT, when their frame is created;
   signals ignored on entry are recorded and cannot be trapped. The first
   `trap` on a signal saves the disposition the shell had; `trap - SIG`
   and `trap_store_clear()` restore that, not `SIG_DFL`.
   **Interactive signals** (POSIX): `exec_install_interactive_signals()`
   installs a SIGINT handler without `SA_RESTART` that sets
   `executor->sigint_received`, and ignores SIGQUIT and SIGTERM. Waits
   retry on `EINTR`; the simple-command wrapper turns a pending
   `sigint_received` into a `TOP` flow with status 130, and the REPL
   (step 2b) clears the flag, resets the parser and prompts again.
   `exec_child_signals(executor, background)` runs in every forked child
   (subshell, pipeline, command substitution, external command) and
   restores the defaults, with INT and QUIT ignored in background jobs.
5. **Arena memory.** Everything allocated with `xmalloc`/`xstrdup` is owned by
   the arena and must be released with `xfree`; mixing `free()` in (as `cd`
   once did) is a double free at `miga_arena_end()`.
