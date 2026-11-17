#!/bin/sh
# Test: the jobs utility in a non-interactive shell (POSIX XCU "jobs")
# Covers: no jobs; a running background job; -l (process ID) and -p (ID
#         only); two jobs; a job that has ended is reported as Done once
#         and then forgotten; job IDs %1 and %%; an unknown job ID.
#
# Notes:
#   - The current-job marker and the state column's spacing differ between
#     shells ("[1] + Running" in dash, "[1]+  Running" in bash), so the
#     tests check the "[n]" prefix and the state word.
#   - Whether a job collected by "wait" is still reported differs (dash:
#     Done, bash: nothing) and is not tested.
#   - "jobs" output is sent to a file: in a command substitution it would
#     run in a subshell.
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the jobs utility (non-interactive)...\n"

WORK="${TMPDIR:-/tmp}/jobs_test_$$"
mkdir -p "$WORK"
J="$WORK/jobs.out"

jobs > "$J"
assert_eq "0" "$?" "jobs with no jobs: status 0"
assert_eq "" "$(cat "$J")" "jobs with no jobs: no output"

sleep 0.4 &
pid=$!
jobs > "$J"
line=$(head -n 1 "$J")
case "$line" in
    "[1]"*Running*) assert_eq "ok" "ok" "a running background job is listed as [1] Running" ;;
    *) assert_eq "[1] ... Running" "$line" "a running background job is listed as [1] Running" ;;
esac
assert_eq "1" "$(wc -l < "$J" | tr -d ' ')" "one job, one line"
jobs -l > "$J"
case "$(cat "$J")" in
    *"$pid"*) assert_eq "ok" "ok" "jobs -l shows the process ID" ;;
    *) assert_eq "contains $pid" "$(cat "$J")" "jobs -l shows the process ID" ;;
esac
jobs -p > "$J"
assert_eq "$pid" "$(cat "$J")" "jobs -p prints the process ID alone"
jobs %1 > "$J"
case "$(cat "$J")" in
    "[1]"*) assert_eq "ok" "ok" "jobs %1 selects job 1" ;;
    *) assert_eq "[1] ..." "$(cat "$J")" "jobs %1 selects job 1" ;;
esac
jobs %% > "$J"
case "$(cat "$J")" in
    "[1]"*) assert_eq "ok" "ok" "jobs %% selects the current job" ;;
    *) assert_eq "[1] ..." "$(cat "$J")" "jobs %% selects the current job" ;;
esac
jobs %zz > "$J" 2>/dev/null
st=$?
if [ "$st" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "jobs with an unknown job ID fails"
else
    assert_eq "nonzero" "0" "jobs with an unknown job ID fails"
fi
wait

sleep 0.4 &
sleep 0.4 &
jobs > "$J"
assert_eq "2" "$(wc -l < "$J" | tr -d ' ')" "two jobs, two lines"
case "$(cat "$J")" in
    *"[1]"*"[2]"*|*"[2]"*"[1]"*) assert_eq "ok" "ok" "both job numbers are listed" ;;
    *) assert_eq "[1] and [2]" "$(cat "$J")" "both job numbers are listed" ;;
esac
wait

# a job that has ended on its own is reported as Done once
sleep 0.1 &
sleep 0.4
jobs > "$J"
case "$(cat "$J")" in
    "["*Done*) assert_eq "ok" "ok" "an ended job is reported as Done" ;;
    *) assert_eq "[n] ... Done" "$(cat "$J")" "an ended job is reported as Done" ;;
esac
jobs > "$J"
assert_eq "" "$(cat "$J")" "a reported job is forgotten"

rm -rf "$WORK"
finish_tests
