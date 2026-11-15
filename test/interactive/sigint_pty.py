#!/usr/bin/env python3
"""Interactive SIGINT behaviour of a POSIX shell, driven through a pseudo-terminal.

Usage: sigint_pty.py /path/to/shell [args...]

The shell is started on a pty, so it is interactive and the terminal line
discipline delivers SIGINT to the foreground process group. Each scenario
types a line, sends Ctrl-C where needed, and checks the transcript:

  1. Ctrl-C at an empty prompt: the shell survives, $? is 130.
  2. Ctrl-C with a partial line typed: the line is discarded.
  3. Ctrl-C during "sleep": the command ends, the prompt returns at once, $? is 130.
  4. Ctrl-C during a loop: the whole loop stops, not just the current command.
  5. Ctrl-C during a subshell: same.
  6. A background job survives the user's Ctrl-C.
  7. trap on INT: Ctrl-C during sleep runs the action and the shell goes on.
  8. trap - INT: Ctrl-C kills the command again.
  9. "exit" ends the shell with the requested status.

Exit status: 0 when every check passes, 1 otherwise. Writes a PASS/FAIL line
per check, like the shell test suite. Validated against dash (9/9) and bash
(8/9: bash does not run an INT trap when the foreground child, not the
shell itself, was the one killed by Ctrl-C; dash and migash run it).
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptyshell import PtyShell, check, finish, exit_status

shell_argv = sys.argv[1:] if len(sys.argv) > 1 else ["sh"]

print("  Testing interactive SIGINT handling (pty)...")
sh = PtyShell(shell_argv)

# 1. Ctrl-C at an empty prompt
sh.ctrl("c")
alive, _ = sh.wait_prompt()
out = sh.run('echo "st=$?"')
check(alive and "st=130" in out, "Ctrl-C at an empty prompt: new prompt, $? is 130", out)

# 2. Ctrl-C with a partial line typed
sh.take()
sh.send("echo partial_line_should_be_discarded")
sh.drain(0.3)
sh.ctrl("c")
alive, _ = sh.wait_prompt()
out = sh.run("echo after_partial")
check(alive and "\nafter_partial\n" in out and "partial_line_should" not in out,
      "Ctrl-C discards a partially typed line", out)

# 3. Ctrl-C during a foreground command
back, elapsed, _ = sh.interrupt_during("sleep 10")
out = sh.run('echo "st=$?"')
check(back and elapsed < 2.5 and "st=130" in out,
      "Ctrl-C ends a foreground command and returns to the prompt with $? 130",
      "elapsed=%.1fs out=%r" % (elapsed, out))

# 4. Ctrl-C during a loop stops the loop
back, elapsed, _ = sh.interrupt_during("n=0; while true; do sleep 1; n=$((n+1)); done", settle=1.5)
out = sh.run('echo "loop_done n=$n"')
check(back and elapsed < 2.5 and "loop_done" in out,
      "Ctrl-C during a loop stops the whole loop", "elapsed=%.1fs out=%r" % (elapsed, out))

# 5. Ctrl-C during a subshell
back, elapsed, _ = sh.interrupt_during("(sleep 10; echo not_reached)")
out = sh.run('echo "sub st=$?"')
check(back and elapsed < 2.5 and "sub st=130" in out and "not_reached" not in out,
      "Ctrl-C ends a subshell command", "elapsed=%.1fs out=%r" % (elapsed, out))

# 6. A background job survives Ctrl-C
sh.run("sleep 30 & bgpid=$!")
sh.ctrl("c")
sh.wait_prompt()
out = sh.run('kill -0 "$bgpid" 2>/dev/null && echo bg_alive || echo bg_dead; kill "$bgpid" 2>/dev/null')
check("bg_alive" in out, "a background job survives Ctrl-C", out)

# 7. trap on INT runs the action; the shell continues
sh.run("trap 'echo caught_int' INT")
back, elapsed, out = sh.interrupt_during("sleep 10")
out2 = sh.run('echo "after_trap st=$?"')
check(back and "caught_int\n" in out and "\nafter_trap st=" in out2,
      "trap on INT runs the action after Ctrl-C and the shell goes on", out + out2)

# 8. trap - INT restores the default behaviour
sh.run("trap - INT")
back, elapsed, out = sh.interrupt_during("sleep 10")
out2 = sh.run('echo "reset st=$?"')
check(back and "reset st=130" in out2 and "caught_int" not in out + out2,
      "trap - INT: Ctrl-C ends the command again without the action", out + out2)

# 9. exit
sh.take()
sh.send("exit 7\n")
status = sh.wait_exit()
check(status is not None and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 7,
      "exit ends the interactive shell with the requested status", exit_status(status))

finish()
