"""Drive a shell on a pseudo-terminal for the interactive tests.

A PtyShell starts the shell on a pty (so it is interactive and the line
discipline delivers signals), optionally gives it a recognisable PS1, and
offers a few primitives: send text, wait for output, run a command and
collect its output. Results are reported with check()/finish(), one PASS/FAIL
line per check like the shell test suite.
"""
import os, pty, select, signal, sys, time

PROMPT = b"MIGA_PS1> "
MARK = "__M__"

_failures = 0
_checks = 0


def check(ok, name, detail=""):
    """Record one check and print its PASS/FAIL line."""
    global _failures, _checks
    _checks += 1
    if ok:
        print("    PASS", name)
    else:
        _failures += 1
        print("    FAIL", name)
        if detail:
            text = detail if isinstance(detail, str) else repr(detail)
            print("      " + text.replace("\r\n", "\n").replace("\n", "\n      "))


def finish():
    """Print the summary and exit 0 (all passed) or 1."""
    if _failures:
        print("  %d/%d checks failed" % (_failures, _checks))
        sys.exit(1)
    print("  %d checks passed" % _checks)
    sys.exit(0)


def clean(data):
    """Drop terminal control sequences some shells emit around prompts
    (bash's bracketed-paste toggles) and carriage returns."""
    return data.replace(b"\x1b[?2004h", b"").replace(b"\x1b[?2004l", b"").replace(b"\r", b"")


class PtyShell:
    def __init__(self, argv, env=None, stdout_path=None, set_prompt=True, prompt=PROMPT):
        """Start argv on a new pty. env adds to (or, for ENV, replaces) the
        inherited environment; stdout_path redirects the shell's standard
        output to that file so that only standard error reaches the pty.
        With set_prompt, PS1 is set to `prompt` from inside the shell (not
        every shell takes PS1 from the environment) and the first prompt is
        awaited."""
        self.argv = argv
        self.prompt = prompt
        self.buf = b""
        self.status = None
        pid, fd = pty.fork()
        if pid == 0:
            os.environ.pop("ENV", None)
            if env:
                os.environ.update(env)
            if stdout_path:
                f = os.open(stdout_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                os.dup2(f, 1)
                os.close(f)
            try:
                os.execvp(argv[0], argv)
            finally:
                os._exit(127)
        self.pid = pid
        self.fd = fd
        if set_prompt:
            time.sleep(0.5)
            self.take()
            self.send("PS1='" + prompt.decode() + "'\n")
            ok, _ = self.wait_prompt(5.0)
            if not ok:
                print("    FAIL the shell did not show a prompt on the pty")
                self.close()
                sys.exit(1)

    # -- raw transcript -----------------------------------------------------
    def read_until(self, pattern, timeout):
        """Collect output until pattern appears in the transcript or timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if pattern in self.buf:
                return True
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if r:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    return pattern in self.buf
                if not data:
                    return pattern in self.buf
                self.buf += clean(data)
        return pattern in self.buf

    def drain(self, seconds):
        """Collect whatever arrives within the given time."""
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if r:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    break
                if not data:
                    break
                self.buf += clean(data)
        return self.take()

    def take(self):
        """Return and clear the transcript collected so far."""
        text = self.buf.decode(errors="replace")
        self.buf = b""
        return text

    def send(self, text):
        os.write(self.fd, text.encode() if isinstance(text, str) else text)

    def ctrl(self, letter):
        """Send a control character (ctrl('c') is SIGINT, ctrl('d') end of file)."""
        os.write(self.fd, bytes([ord(letter.lower()) - ord("a") + 1]))

    # -- higher level -------------------------------------------------------
    def wait_prompt(self, timeout=3.0):
        """Wait for a fresh prompt, pause briefly (dash may discard a line
        typed immediately after Ctrl-C), return (seen, transcript)."""
        ok = self.read_until(self.prompt, timeout)
        time.sleep(0.2)
        return ok, self.take()

    def run(self, line, timeout=5.0):
        """Type a line, wait for its output (ended by a marker) and the next
        prompt; return the transcript."""
        self.take()
        self.send(line + "; echo " + MARK + "\n")
        # the marker as output starts a line; its echo as typed does not
        self.read_until(b"\n" + MARK.encode(), timeout)
        self.read_until(self.prompt, timeout)
        return self.take()

    def interrupt_during(self, line, settle=0.6, timeout=3.0):
        """Type a line, send Ctrl-C once it has started, wait for the prompt.
        Returns (prompt_seen, seconds_until_prompt, transcript)."""
        self.take()
        self.send(line + "\n")
        time.sleep(settle)
        t0 = time.time()
        self.ctrl("c")
        ok = self.read_until(self.prompt, timeout)
        return ok, time.time() - t0, self.take()

    def wait_exit(self, timeout=5.0):
        """Wait for the shell to exit; return the wait status, or None (and
        kill it) if it is still running after the timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            wpid, wstatus = os.waitpid(self.pid, os.WNOHANG)
            if wpid == self.pid:
                self.status = wstatus
                return wstatus
            self.drain(0.1)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        self.status = None
        return None

    def close(self):
        """End the session: ask the shell to exit, then make sure it is gone."""
        if self.status is not None:
            return
        try:
            self.send("exit\n")
        except OSError:
            pass
        self.wait_exit(3.0)
        try:
            os.close(self.fd)
        except OSError:
            pass


def exit_status(wstatus):
    """Describe a wait status for a check's detail line."""
    if wstatus is None:
        return "still running"
    if os.WIFEXITED(wstatus):
        return "exit %d" % os.WEXITSTATUS(wstatus)
    if os.WIFSIGNALED(wstatus):
        return "signal %d" % os.WTERMSIG(wstatus)
    return "wstatus %r" % wstatus
