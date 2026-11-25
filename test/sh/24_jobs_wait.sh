#!/bin/sh
# Test: jobs and wait in a non-interactive shell (POSIX XCU 2.9.3.1, "jobs",
#       "wait", "kill")
# Covers: the job table of a script (asynchronous lists get job IDs that
#         "jobs", "kill" and "wait" accept), fg/bg refusing to work without
#         job control, and the status "wait" returns for a pid or job ID,
#         also when the job finished before wait was called, and the reuse
#         of job numbers once jobs are gone, and the job ID forms %%, %+,
#         %-, %string and %?string, and "jobs" reporting finished jobs as
#         such, then forgetting them.
#
# Notes:
#   - Each check runs in its own $SH -c so that job numbers start at 1.
#   - Expected values were checked against bash --posix; dash does not
#     signal a job by job ID without job control.

. "$(dirname "$0")/test_helpers.sh"

SH="${TEST_SHELL:-sh}"

printf "  Testing jobs and wait in a script...\n"

# -----------------------------
# 1. The job table of a script
# -----------------------------
result=$($SH -c 'sleep 1 & jobs | wc -l; kill %1')
assert_eq "1" "$(echo $result)" "jobs lists an asynchronous list in a script"

result=$($SH -c 'sleep 5 & kill %1 && echo sent')
assert_eq "sent" "$result" "kill %N signals a job in a script"

result=$($SH -c 'fg' 2>&1)
case $result in *"no job control"*) result=refused ;; esac
assert_eq "refused" "$result" "fg reports that there is no job control"

# -----------------------------
# 2. The status wait returns
# -----------------------------
result=$($SH -c '(exit 4) & p=$!; wait $p; echo $?')
assert_eq "4" "$result" "wait pid returns the job's exit status"

result=$($SH -c '(exit 5) & p=$!; sleep 1; : ; : ; wait $p; echo $?')
assert_eq "5" "$result" "wait pid returns the status of a job that already finished"

result=$($SH -c '(exit 3) & wait %1; echo $?')
assert_eq "3" "$result" "wait %N returns the job's exit status"

result=$($SH -c 'sleep 5 & kill %1; wait %1; echo $?')
assert_eq "143" "$result" "wait returns 128 + the signal number for a killed job"

result=$($SH -c '(exit 6) | (exit 7) & wait %1; echo $?')
assert_eq "7" "$result" "wait %N returns the status of the last command of a pipeline"

result=$($SH -c '(exit 2) & wait %1; jobs; echo end')
assert_eq "end" "$result" "a job is forgotten once wait has returned its status"

# -----------------------------
# 3. Job numbers
# -----------------------------
result=$($SH -c '(exit 2) & wait %1; (exit 3) & wait %1; echo $?')
assert_eq "3" "$result" "job numbers start again at 1 once every job is gone"

result=$($SH -c 'sleep 2 & sleep 2 & kill %1; wait %1; sleep 2 & jobs | sed "s/].*/]/" | sort; kill %2 %3')
assert_eq "[2]
[3]" "$result" "a new job is numbered one past the highest job number in use"

# -----------------------------
# 4. Job ID forms
# -----------------------------
result=$($SH -c '(exit 3) & wait %%; echo $?')
assert_eq "3" "$result" "%% is the current job"

result=$($SH -c '(exit 4) & (exit 5) & wait %-; echo $?; wait %+; echo $?')
assert_eq "4
5" "$result" "%- is the previous job and %+ the current one"

result=$($SH -c 'sleep 1 & (exit 6) & wait %sleep; echo $?')
assert_eq "0" "$result" "%string is the job whose command begins with string"

result=$($SH -c '(exit 7) & wait %?xit; echo $?')
assert_eq "7" "$result" "%?string is the job whose command contains string"

result=$($SH -c 'wait %nomatch' 2>/dev/null; echo $?)
assert_eq "127" "$result" "wait for an unknown job ID returns 127"

# -----------------------------
# 5. jobs reports finished jobs, then forgets them
# -----------------------------
# "jobs" itself must run in the shell that started the job (not in a pipeline
# or command substitution), so its output goes to a file.
result=$($SH -c 'f=${TMPDIR:-/tmp}/jobs_out_$$; (exit 6) & sleep 1; jobs > "$f"; grep -c Running "$f"; rm -f "$f"')
assert_eq "0" "$result" "jobs does not report a job that finished during the same command as running"

result=$($SH -c '(exit 6) & sleep 1; jobs >/dev/null; jobs; echo end')
assert_eq "end" "$result" "a finished job is forgotten once jobs has reported it"

finish_tests
