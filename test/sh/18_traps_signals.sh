#!/bin/sh
# Test: trap and signal handling (POSIX XCU 2.11 Signals and Error Handling,
#       and the trap special built-in utility)
# Covers: setting, listing, resetting and ignoring traps; the EXIT trap;
#         when trap actions run and what $? is afterwards; expansion time of
#         the action; signals ignored on entry; SIGINT with and without a trap.
#
# Notes:
#   - Linux signal numbers are not portable, so conditions are named (USR1,
#     USR2, INT, EXIT), never numbered, except 0 for EXIT.
#   - Tests that must observe a shell dying run a child shell. The runner
#     exports TEST_SHELL; run directly, the script falls back to "sh".
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing trap and signal handling (POSIX 2.11, trap)...\n"

SH="${TEST_SHELL:-sh}"

# USR1 as a number for exit-status checks: a child killed by a signal exits
# with 128 + signal number, which we learn from the system's kill utility.
USR1_NUM=$(kill -l USR1 2>/dev/null || echo 10)

# -----------------------------
# 1. A trap action runs when the signal arrives
# -----------------------------
result=""
trap 'result="caught"' USR1
kill -USR1 $$
assert_eq "caught" "$result" "trap action runs on the trapped signal"

# The action runs after the command during which the signal arrived
# completes, and before the next command starts.
seq=""
trap 'seq="${seq}T"' USR1
kill -USR1 $$; seq="${seq}N"
assert_eq "TN" "$seq" "trap action runs before the next command"

# $? after the trap action is what it was before the action ran
trap 'false' USR1
kill -USR1 $$
assert_eq "0" "$?" "\$? is restored after a trap action"

# -----------------------------
# 2. Several conditions in one trap command; the SIG prefix is not needed
# -----------------------------
hits=0
trap 'hits=$((hits + 1))' USR1 USR2
kill -USR1 $$
kill -USR2 $$
assert_eq "2" "$hits" "one trap command can name several conditions"

# -----------------------------
# 3. The action is a command string, expanded when it runs, not when set
# -----------------------------
v=early
trap 'late=$v' USR1
v=late_value
kill -USR1 $$
assert_eq "late_value" "$late" "single-quoted action is expanded when it runs"

v=early
trap "early=$v" USR1
v=changed
kill -USR1 $$
assert_eq "early" "$early" "double-quoted action is expanded when set"

# A function can be the action
f_called=0
mark_called() { f_called=1; }
trap mark_called USR1
kill -USR1 $$
assert_eq "1" "$f_called" "a function name works as a trap action"

# A trap set inside a function is global (there are no function-local traps)
set_in_function() { trap 'from_fn=yes' USR1; }
set_in_function
kill -USR1 $$
assert_eq "yes" "$from_fn" "trap set inside a function applies to the shell"

# -----------------------------
# 4. trap with no operands lists the current traps
# -----------------------------
# (Written to a file: in $(trap) or "trap | grep" the trap command runs in a
# subshell, where traps have been reset. POSIX lets $(trap) show the parent's
# traps but does not require it, and dash does not.)
trap 'echo listed-action' USR1
listing_file="${TMPDIR:-/tmp}/traps_$$.txt"
trap > "$listing_file"
listing=$(cat "$listing_file")
rm -f "$listing_file"
case $listing in
    *listed-action*USR1*) assert_eq "ok" "ok" "trap with no operands lists the traps" ;;
    *) assert_eq "trap -- 'echo listed-action' USR1" "$listing" "trap with no operands lists the traps" ;;
esac

# -----------------------------
# 5. trap - resets to the default action; "" ignores
# -----------------------------
# Default USR1 kills the shell: status 128 + signal number
$SH -c 'trap "echo reset-ran" USR1; trap - USR1; kill -USR1 $$; echo survived' >/dev/null 2>&1
assert_eq "$((128 + USR1_NUM))" "$?" "trap - restores the default action (shell dies)"

# "trap -- - USR1" is the same thing
$SH -c 'trap "echo x" USR1; trap -- - USR1; kill -USR1 $$; echo survived' >/dev/null 2>&1
assert_eq "$((128 + USR1_NUM))" "$?" "trap -- - also resets"

result=$($SH -c 'trap "" USR1; kill -USR1 $$; echo survived' 2>/dev/null)
assert_eq "survived" "$result" "trap \"\" ignores the signal"

# An ignored signal stays ignored in child processes
result=$($SH -c 'trap "" USR1; sh -c "kill -USR1 \$\$; echo child_survived"' 2>/dev/null)
assert_eq "child_survived" "$result" "ignored signals are inherited by children"

# Signals ignored on entry to a non-interactive shell cannot be trapped
result=$($SH -c 'trap "" USR1; '"$SH"' -c "trap \"echo trapped\" USR1; kill -USR1 \$\$; echo done"' 2>/dev/null)
assert_eq "done" "$result" "a signal ignored on entry cannot be trapped"

# -----------------------------
# 6. Status of the trap utility
# -----------------------------
trap 'true' USR1
assert_eq "0" "$?" "trap returns 0 on success"
trap 'true' NOSUCHSIGNAL 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "trap returns non-zero for an unknown condition"
else
    assert_eq "nonzero" "$status" "trap returns non-zero for an unknown condition"
fi

trap - USR1 USR2

# -----------------------------
# 7. The EXIT trap
# -----------------------------
result=$($SH -c 'trap "echo bye" EXIT; echo main')
assert_eq "main
bye" "$result" "EXIT trap runs when the shell exits"

result=$($SH -c 'trap "echo zero" 0; echo main')
assert_eq "main
zero" "$result" "condition 0 is EXIT"

# The exit status is preserved across the EXIT trap ...
$SH -c 'trap "echo in-trap" EXIT; exit 3' >/dev/null 2>&1
assert_eq "3" "$?" "exit status is preserved across the EXIT trap"

# ... and the EXIT trap sees it in $?
result=$($SH -c 'trap "echo status=\$?" EXIT; false')
assert_eq "status=1" "$result" "EXIT trap sees the exit status in \$?"

# ... unless the trap action itself calls exit
$SH -c 'trap "exit 5" EXIT; exit 2' >/dev/null 2>&1
assert_eq "5" "$?" "exit inside the EXIT trap sets the status"

# exit inside the EXIT trap does not run the trap again
result=$($SH -c 'trap "echo once; exit" EXIT; exit')
assert_eq "once" "$result" "EXIT trap runs only once"

# A subshell runs its own EXIT trap when it exits
result=$( ( trap 'echo sub-exit' EXIT; : ) )
assert_eq "sub-exit" "$result" "a subshell runs its EXIT trap"

# The EXIT trap also runs when the shell is killed by a trapped signal that
# exits ...
result=$($SH -c 'trap "echo exit-trap" EXIT; trap "exit 4" USR1; kill -USR1 $$; echo not-reached')
assert_eq "exit-trap" "$result" "EXIT trap runs after a signal trap that exits"

# ... and when the script simply ends
result=$($SH -c 'trap "echo at-end" EXIT; :')
assert_eq "at-end" "$result" "EXIT trap runs at end of script"

# trap - EXIT removes it
result=$($SH -c 'trap "echo removed" EXIT; trap - EXIT; echo main')
assert_eq "main" "$result" "trap - EXIT removes the EXIT trap"

# -----------------------------
# 8. SIGINT
# -----------------------------
# Without a trap, SIGINT kills a non-interactive shell: status 128 + 2
$SH -c 'kill -INT $$; echo survived' >/dev/null 2>&1
assert_eq "130" "$?" "untrapped SIGINT kills a non-interactive shell with 130"

# A trap on INT runs instead
result=$($SH -c 'trap "echo int-trap" INT; kill -INT $$; echo after')
assert_eq "int-trap
after" "$result" "trapped SIGINT runs the action and continues"

# A trap that exits ends the script with its status
result=$($SH -c 'trap "echo int; exit 7" INT; kill -INT $$; echo not-reached')
assert_eq "int" "$result" "INT trap that exits stops the script"
$SH -c 'trap "exit 7" INT; kill -INT $$; echo not-reached' >/dev/null 2>&1
assert_eq "7" "$?" "INT trap that exits sets the exit status"

# trap "" INT makes the shell immune
result=$($SH -c 'trap "" INT; kill -INT $$; echo immune' 2>/dev/null)
assert_eq "immune" "$result" "trap \"\" INT ignores SIGINT"

# SIGINT delivered to a foreground child does not kill the shell itself:
# the child dies (130), the shell goes on and sees the status.
result=$($SH -c 'sh -c "kill -INT \$\$"; echo "child=$?"' 2>/dev/null)
assert_eq "child=130" "$result" "SIGINT to a foreground child reports 130 and the shell continues"

# The INT trap in the parent does not fire for a signal sent to the child
result=$($SH -c 'trap "echo parent-trap" INT; sh -c "kill -INT \$\$"; echo "child=$?"' 2>/dev/null)
assert_eq "child=130" "$result" "a signal to the child does not run the parent's trap"

finish_tests
