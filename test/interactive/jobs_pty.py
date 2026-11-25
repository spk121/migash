#!/usr/bin/env python3
"""The interactive shell forgets jobs that "wait" has collected.

Usage: jobs_pty.py /path/to/shell [args...]

POSIX XCU "wait": once wait has obtained the exit status of a job, the shell
removes that job from its list of known jobs. Checked on a pty, where job
control is on and the job table is used:

  1. After "cmd &" and "wait" (no operands), "jobs" lists nothing.
  2. After "cmd &" and "wait $!", "jobs" lists nothing.
  3. After "cmd &" and "wait %N" (N from "jobs"), "jobs" lists nothing, and
     "wait %N" returns the job's exit status.
  4. Several background jobs collected with "wait" leave the job list empty
     (the table used to keep every finished job until the shell exited).

Exit status: 0 when every check passes, 1 otherwise. Validated against
bash --posix.
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptyshell import PtyShell, check, finish

shell_argv = sys.argv[1:] if len(sys.argv) > 1 else ["sh"]

print("  Testing that wait forgets collected jobs (pty)...")

sh = PtyShell(shell_argv)


# PtyShell.run() appends "; echo <marker>", so a line that would end in "&"
# gets a trailing ":" ("cmd & :").


def jobs_listing():
    """The output of "jobs", without the echoed command line."""
    out = sh.run("jobs")
    prompt = sh.prompt.decode()
    lines = [l for l in out.splitlines()
             if l.strip() and "__M__" not in l and not l.startswith("jobs") and not l.startswith(prompt)]
    return "\n".join(lines)


sh.run("sleep 0 & :")
sh.run("wait")
listing = jobs_listing()
check(listing == "", 'after "cmd &" and "wait", "jobs" lists nothing', listing)

sh.run("sleep 0 & :")
sh.run("wait $!")
listing = jobs_listing()
check(listing == "", 'after "cmd &" and "wait $!", "jobs" lists nothing', listing)

sh.run("(sleep 1; exit 3) & :")
m = re.search(r"\[(\d+)\]", jobs_listing())
job = m.group(1) if m else "1"
out = sh.run('wait %' + job + '; echo "st=$?"')
check("st=3" in out, '"wait %N" returns the job\'s exit status', out)
listing = jobs_listing()
check(listing == "", 'after "cmd &" and "wait %N", "jobs" lists nothing', listing)

for _ in range(5):
    sh.run("sleep 0 & :")
    sh.run("wait")
listing = jobs_listing()
check(listing == "", "five background jobs collected by wait leave no jobs behind", listing)

sh.close()
finish()
