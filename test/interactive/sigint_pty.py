#!/usr/bin/env python3
"""Interactive SIGINT behaviour of a POSIX shell, driven through a pseudo-terminal.

Usage: sigint_pty.py /path/to/shell

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
import os, pty, select, signal, sys, time

shell_argv = sys.argv[1:] if len(sys.argv) > 1 else ["sh"]
shell = shell_argv[0]
failures = 0
checks = 0
PROMPT = b"MIGA_PS1> "
MARK = "__M__"


def result(ok, name, detail=""):
    global failures, checks
    checks += 1
    if ok:
        print("    PASS", name)
    else:
        failures += 1
        print("    FAIL", name)
        if detail:
            print("      " + detail.replace("\r\n", "\n").replace("\n", "\n      "))


pid, fd = pty.fork()
if pid == 0:
    os.environ.pop("ENV", None)
    os.environ["PS1"] = PROMPT.decode()
    os.execvp(shell, shell_argv)

buf = b""


def read_until(pattern, timeout):
    """Collect output until pattern appears (after the current buffer) or timeout."""
    global buf
    deadline = time.time() + timeout
    while time.time() < deadline:
        if pattern in buf:
            return True
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                data = os.read(fd, 65536)
            except OSError:
                return pattern in buf
            if not data:
                return pattern in buf
            # bash wraps interactive prompts in bracketed-paste sequences
            buf += data.replace(b"\x1b[?2004h", b"").replace(b"\x1b[?2004l", b"")
    return pattern in buf


def take():
    global buf
    text = buf.decode(errors="replace")
    buf = b""
    return text


def send(text):
    os.write(fd, text.encode())


def wait_prompt(timeout=3.0):
    """Wait for a fresh prompt, then return and clear the transcript. A short
    pause lets the shell finish its SIGINT handling before more input (dash
    may otherwise discard a line typed immediately after Ctrl-C)."""
    ok = read_until(PROMPT, timeout)
    time.sleep(0.2)
    return ok, take()


def run(line, timeout=5.0):
    """Type a line, wait for its output (ended by a marker) and the next prompt."""
    take()
    send(line + "; echo " + MARK + "\n")
    # the marker as output starts a line; its echo as typed does not
    read_until(b"\n" + MARK.encode(), timeout)
    read_until(PROMPT, timeout)
    return take()


def interrupt_during(line, settle=0.6, timeout=3.0):
    """Type a line, send Ctrl-C after it has started, wait for the prompt."""
    take()
    send(line + "\n")
    time.sleep(settle)
    t0 = time.time()
    os.write(fd, b"\x03")
    ok = read_until(PROMPT, timeout)
    return ok, time.time() - t0, take()


print("  Testing interactive SIGINT handling (pty)...")

# Set a recognisable prompt from inside the shell (not every shell takes PS1
# from the environment), then wait for it.
time.sleep(0.5)
take()
send("PS1='" + PROMPT.decode() + "'\n")
ok, _ = wait_prompt(5.0)
if not ok:
    print("    FAIL the shell did not show a prompt on the pty")
    sys.exit(1)

# 1. Ctrl-C at an empty prompt
os.write(fd, b"\x03")
alive, _ = wait_prompt()
out = run('echo "st=$?"')
result(alive and "st=130" in out, "Ctrl-C at an empty prompt: new prompt, $? is 130", out)

# 2. Ctrl-C with a partial line typed
take()
send("echo partial_line_should_be_discarded")
time.sleep(0.3)
os.write(fd, b"\x03")
alive, _ = wait_prompt()
out = run("echo after_partial")
result(alive and "after_partial" in out and "partial_line_should" not in out,
       "Ctrl-C discards a partially typed line", out)

# 3. Ctrl-C during a foreground command
back, elapsed, _ = interrupt_during("sleep 10")
out = run('echo "st=$?"')
result(back and elapsed < 2.5 and "st=130" in out,
       "Ctrl-C ends a foreground command and returns to the prompt with $? 130",
       "elapsed=%.1fs out=%r" % (elapsed, out))

# 4. Ctrl-C during a loop stops the loop
back, elapsed, _ = interrupt_during("n=0; while true; do sleep 1; n=$((n+1)); done", settle=1.5)
out = run('echo "loop_done n=$n"')
result(back and elapsed < 2.5 and "loop_done" in out,
       "Ctrl-C during a loop stops the whole loop", "elapsed=%.1fs out=%r" % (elapsed, out))

# 5. Ctrl-C during a subshell
back, elapsed, _ = interrupt_during("(sleep 10; echo not_reached)")
out = run('echo "sub st=$?"')
result(back and elapsed < 2.5 and "sub st=130" in out and "not_reached" not in out,
       "Ctrl-C ends a subshell command", "elapsed=%.1fs out=%r" % (elapsed, out))

# 6. A background job survives Ctrl-C
run("sleep 30 & bgpid=$!")
os.write(fd, b"\x03")
wait_prompt()
out = run('kill -0 "$bgpid" 2>/dev/null && echo bg_alive || echo bg_dead; kill "$bgpid" 2>/dev/null')
result("bg_alive" in out, "a background job survives Ctrl-C", out)

# 7. trap on INT runs the action; the shell continues
run("trap 'echo caught_int' INT")
back, elapsed, out = interrupt_during("sleep 10")
out2 = run('echo "after_trap st=$?"')
result(back and "caught_int" in out and "after_trap" in out2,
       "trap on INT runs the action after Ctrl-C and the shell goes on", out + out2)

# 8. trap - INT restores the default behaviour
run("trap - INT")
back, elapsed, out = interrupt_during("sleep 10")
out2 = run('echo "reset st=$?"')
result(back and "reset st=130" in out2 and "caught_int" not in out + out2,
       "trap - INT: Ctrl-C ends the command again without the action", out + out2)

# 9. exit
take()
send("exit 7\n")
status = None
for _ in range(50):
    wpid, wstatus = os.waitpid(pid, os.WNOHANG)
    if wpid == pid:
        status = wstatus
        break
    time.sleep(0.1)
if status is None:
    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)
result(status is not None and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 7,
       "exit ends the interactive shell with the requested status", "wstatus=%r" % status)

if failures:
    print("  %d/%d checks failed" % (failures, checks))
    sys.exit(1)
print("  %d checks passed" % checks)
