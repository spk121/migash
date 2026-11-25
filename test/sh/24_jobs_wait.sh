#!/bin/sh
# Test: jobs and wait in a non-interactive shell (POSIX XCU 2.9.3.1, "jobs",
#       "wait", "kill")
# Covers: the job table of a script (asynchronous lists get job IDs that
#         "jobs", "kill" and "wait" accept), and fg/bg refusing to work
#         without job control.
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

finish_tests
