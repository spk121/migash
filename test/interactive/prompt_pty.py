#!/usr/bin/env python3
"""Interactive prompt behaviour of a POSIX shell, driven through a pseudo-terminal.

Usage: prompt_pty.py /path/to/shell [args...]

POSIX XCU 2.5.3 (PS1, PS2, ENV) and 2.8.1 (consequences of shell errors in
an interactive shell), checked on a pty:

  1. The default PS1 is "$ " ("# " for the superuser) and goes to standard error.
  2. PS1 undergoes parameter expansion ("${x:+y}", "$?" is the last status);
     command substitution and arithmetic are left to the implementation and
     not checked.
  3. An empty PS1 prints no prompt.
  4. PS2 ("> " by default, expanded the same way) prompts for the rest of a
     compound command, an unterminated quote, a here-document and a
     backslash-newline continuation; the construct then runs correctly.
  5. End of file at a PS2 prompt is a syntax error; the shell goes on (like
     dash; bash exits instead).
  6. Shell errors do not end an interactive shell (2.8.1): special built-in
     misuse, a bad "set" option, "${x?}", a syntax error, command not found
     (127), a redirection error, "." of a missing file and a failed "exec"
     (bash follows POSIX here; dash exits on the failed exec).
  7. ENV is parameter-expanded and the file it names is sourced before the
     first prompt (its PS1 is used).
  8. "set -o ignoreeof" refuses end of file with a message.
  9. End of file at a PS1 prompt ends the shell with the last command's status;
     "exit" ends it the same way (same final output), with the given status.
 10. Without a terminal: a shell reading commands from a pipe prints no
     prompt; "sh -i" reading from a pipe prints prompts on standard error.

Exit status: 0 when every check passes, 1 otherwise. Validated against dash
(fails 6: exec) and bash --posix (fails 5: EOF at PS2).
"""
import os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptyshell import PROMPT, PtyShell, check, finish, exit_status

shell_argv = sys.argv[1:] if len(sys.argv) > 1 else ["sh"]
work = tempfile.mkdtemp(prefix="prompt_pty.")

print("  Testing interactive prompts (pty)...")

# 1. default prompt, written to standard error
default_ps1 = "# " if os.geteuid() == 0 else "$ "
stdout_file = os.path.join(work, "stdout")
sh = PtyShell(shell_argv, set_prompt=False)
sh.read_until(default_ps1.encode(), 5.0)
start = sh.drain(0.5)
check(start.endswith(default_ps1), "the default prompt is %r" % default_ps1, start)
sh.send("exec >%s\n" % stdout_file)
sh.read_until(default_ps1.encode(), 3.0)
sh.take()
sh.send("echo to_stdout\n")
got = sh.read_until(default_ps1.encode(), 3.0)
seen = sh.take()
sh.send("exit\n")
sh.wait_exit()
with open(stdout_file) as f:
    captured = f.read()
check(got and captured == "to_stdout\n" and "\nto_stdout" not in seen,
      "the prompt goes to standard error, command output to standard output",
      "stdout file=%r pty=%r" % (captured, seen))

# 2. PS1 expansion
sh = PtyShell(shell_argv)
sh.take()
sh.send("PP_X=x; PS1='[${HOME:+h}$PP_X${PP_UNSET:-u}]'\n")
seen = sh.read_until(b"\n[hxu]", 3.0)
check(seen, "PS1 undergoes parameter expansion", sh.take())
sh.send("PS1='<$?> '\n")
sh.read_until(b"<0> ", 3.0)
sh.take()
sh.send("false\n")
seen = sh.read_until(b"\n<1> ", 3.0)
check(seen, "$? in PS1 is the status of the last command", sh.take())
sh.send("PS1='MIGA_PS1> '\n")
sh.wait_prompt()

# 3. empty PS1
sh.send("PS1=''\n")
sh.drain(0.5)
sh.send("echo mark\n")
sh.read_until(b"\nmark\n", 3.0)
out = sh.drain(0.5)
check(out == "echo mark\nmark\n", "an empty PS1 prints no prompt", out)
sh.send("PS1='MIGA_PS1> '\n")
sh.wait_prompt()

# 4. PS2
sh.take()
sh.send("for i in 1; do\n")
seen = sh.read_until(b"\n> ", 3.0)
check(seen, "the default PS2 is \"> \"", sh.take())
sh.send("echo in_loop_$i\ndone\n")
out = sh.run("echo after_loop")
check("in_loop_1" in out, "the command completed at the PS2 prompt runs", out)
sh.run("PS2='[${HOME:+c}$PP_X]'")
sh.take()
sh.send("echo 'a\n")
seen = sh.read_until(b"\n[cx]", 3.0)
sh.send("b'\n")
out = sh.run("echo after_quote")
check(seen and "a\nb\n" in out, "PS2 is expanded; an unterminated quote continues under PS2", out)
sh.take()
sh.send("cat <<EOF\n")
seen = sh.read_until(b"\n[cx]", 3.0)
sh.send("hd_$((1+1))\nEOF\n")
out = sh.run("echo after_heredoc")
check(seen and "hd_2\n" in out, "a here-document body is read under PS2", out)
sh.take()
sh.send("echo back\\\n")
seen = sh.read_until(b"\n[cx]", 3.0)
sh.send("slash\n")
out = sh.run("echo after_bs")
check(seen and "\nbackslash\n" in out, "a backslash-newline continuation is read under PS2", out)
sh.run("PS2='> '")

# 5. EOF at a PS2 prompt
sh.take()
sh.send("for i in 1; do\n")
sh.read_until(b"\n> ", 3.0)
sh.ctrl("d")
sh.read_until(b"yntax error", 3.0)
sh.read_until(sh.prompt, 3.0)
err = sh.take()
out = sh.run("echo alive_after_eof")
check("yntax error" in err and "\nalive_after_eof\n" in out,
      "end of file at a PS2 prompt is a syntax error and the shell goes on", err + out)
sh.close()

# 6. shell errors do not end an interactive shell (POSIX 2.8.1). The failing
# command is typed on a line of its own: a shell may abandon the rest of the
# line after a special built-in error.
sh = PtyShell(shell_argv)


def survives(cmd, name, want=None):
    sh.take()
    sh.send(cmd + "\n")
    sh.read_until(b"\n" + sh.prompt, 3.0)
    err = sh.take()
    out = sh.run('echo "st=$?"')
    sts = [l[3:] for l in out.split("\n") if l.startswith("st=")]
    ok = bool(sts) and sts[-1] != "0"
    if want is not None:
        ok = ok and sts[-1] == str(want)
    check(ok, name, err + out)

survives("shift 5", "shift beyond the parameters: error, the shell goes on")
survives("set -o nosuchopt", "set with an unknown option: error, the shell goes on")
survives("echo ${nope?msg}", "${x?}: error, the shell goes on")
survives("for", "a syntax error: the shell goes on")
survives("nosuch_cmd_zz", "command not found: 127, the shell goes on", 127)
survives("echo x > /nonexistent/dir/f", "a redirection error: the shell goes on")
survives(". /nonexistent/file", ". of a missing file: error, the shell goes on")
survives("exec nosuch_cmd_zz", "a failed exec: 127, the shell goes on", 127)
sh.close()

# 7. ENV
rc = os.path.join(work, "rc")
with open(rc, "w") as f:
    f.write("echo rc_ran\nPS1='FROM_RC> '\n")
sh = PtyShell(shell_argv, env={"ENV": "$PROMPT_PTY_DIR/rc", "PROMPT_PTY_DIR": work}, set_prompt=False)
sh.read_until(b"FROM_RC> ", 5.0)
start = sh.drain(0.5)
check("rc_ran" in start and start.endswith("FROM_RC> ") and start.index("rc_ran") < start.index("FROM_RC> "),
      "ENV is parameter-expanded and sourced before the first prompt", start)
sh.close()

# 8. ignoreeof
sh = PtyShell(shell_argv)
sh.run("set -o ignoreeof")
sh.ctrl("d")
msg = sh.drain(1.0)
out = sh.run("echo still_running")
check('Use "exit"' in msg + out and "\nstill_running\n" in out, "set -o ignoreeof refuses end of file", msg + out)
sh.run("set +o ignoreeof")

# 9. EOF at PS1 exits with the last status
sh.send("(exit 3)\n")
sh.wait_prompt()
sh.ctrl("d")
status = sh.wait_exit()
check(status is not None and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 3,
      "end of file at the prompt ends the shell with the last status", exit_status(status))
sh.close()

# (no PS1 assignment first: no command may run before the end of file)
sh = PtyShell(shell_argv, set_prompt=False)
sh.read_until(default_ps1.encode(), 5.0)
sh.ctrl("d")
status = sh.wait_exit()
check(status is not None and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
      "end of file at the first prompt ends the shell with status 0", exit_status(status))
sh.close()

# 9b. "exit" ends the shell like end of file does: through the same shutdown,
#     so with the same final output (a shell's farewell line, if it has one)
def final_output(end_session):
    s2 = PtyShell(shell_argv)
    s2.take()
    end_session(s2)
    out = s2.drain(1.0)  # read the final output before the pty goes away
    st = s2.wait_exit()
    lines = [l.replace(PROMPT.decode(), "").strip() for l in out.splitlines()]
    lines = [l for l in lines if l and l != "exit 4"]
    s2.close()
    return st, (lines[-1] if lines else "")


eof_status, eof_last = final_output(lambda s2: s2.ctrl("d"))
exit_status_, exit_last = final_output(lambda s2: s2.send("exit 4\n"))
check(exit_status_ is not None and os.WIFEXITED(exit_status_) and os.WEXITSTATUS(exit_status_) == 4
      and exit_last == eof_last,
      "exit ends the shell like end of file, with the given status",
      "%s; last line after exit %r, after end of file %r" % (exit_status(exit_status_), exit_last, eof_last))

# 10. no terminal: pipe without -i prints no prompt; -i prints prompts to stderr
env = dict(os.environ)
env.pop("ENV", None)
env["PS1"] = "PIPE_PS1> "
p = subprocess.run(shell_argv, input="echo hi\n", capture_output=True, text=True, env=env, timeout=20)
check(p.stdout == "hi\n" and p.stderr == "",
      "commands from a pipe: no prompt, no banner", "stdout=%r stderr=%r" % (p.stdout, p.stderr))
p = subprocess.run(shell_argv + ["-i"], input="PS1='PIPE_PS1> '\necho hi\n", capture_output=True,
                   text=True, env=env, timeout=20)
check("hi\n" in p.stdout and "PIPE_PS1> " in p.stderr,
      "-i with commands from a pipe: prompts on standard error", "stdout=%r stderr=%r" % (p.stdout, p.stderr))

finish()
