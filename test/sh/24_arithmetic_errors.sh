#!/bin/sh
# Test: arithmetic expansion errors (POSIX XCU 2.6.4, 2.8.1)
# Covers: division and modulo by zero and malformed expressions are
#         expansion errors: a diagnostic on standard error, nothing on
#         standard output, the command is not run, a non-interactive shell
#         exits with a non-zero status (even inside a function, a list or
#         "||"), a subshell's failure does not end its parent; valid
#         expressions are unaffected. The interactive case is in
#         test/interactive/prompt_pty.py.
#
# Notes:
#   - The exit status after an expansion error is only required to be
#     non-zero (dash: 2, bash: 1 or 127), so the tests check "nonzero".
#   - Empty "$(( ))" and a non-numeric variable differ between dash and
#     bash and are not tested.
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing arithmetic expansion errors...\n"

SH="${TEST_SHELL:-sh}"

# Status helper: "nonzero" or the number
nz() { if [ "$1" -ne 0 ]; then echo nonzero; else echo "$1"; fi; }

# --- valid expressions are unaffected ---------------------------------------
assert_eq "0 1 1 -3" "$((x)) $((1/1)) $((7%3)) $((-7/2))" "valid expressions evaluate"

# --- division by zero ---------------------------------------------------------
out=$( (echo "a $((1/0)) b"; echo after) 2>/dev/null )
assert_eq "" "$out" "division by zero: the command does not run and the shell exits"
( echo $((1/0)) ) 2>/dev/null
assert_eq "nonzero" "$(nz $?)" "division by zero: non-zero exit status"
err=$( (echo $((1/0))) 2>&1 >/dev/null )
if [ -n "$err" ]; then
    assert_eq "diagnostic" "diagnostic" "division by zero: a diagnostic on standard error"
else
    assert_eq "diagnostic" "" "division by zero: a diagnostic on standard error"
fi
case "$err" in
    *"[ERROR]"*) assert_eq "no log prefix" "$err" "the diagnostic is a shell message, not a log line" ;;
    *) assert_eq "ok" "ok" "the diagnostic is a shell message, not a log line" ;;
esac

# --- modulo by zero, in an assignment, not caught by || -----------------------
out=$( (x=$((5%0)) || echo caught; echo after) 2>/dev/null )
assert_eq "" "$out" "modulo by zero in an assignment: fatal, not caught by ||"

# --- malformed expressions ----------------------------------------------------
out=$( (echo $((1+)); echo in) 2>/dev/null )
assert_eq "" "$out" "missing operand: fatal"
( echo $((1+)) ) 2>/dev/null
assert_eq "nonzero" "$(nz $?)" "missing operand: non-zero status"
out=$( (echo $((1 2)); echo in) 2>/dev/null )
assert_eq "" "$out" "two operands without an operator: fatal"
out=$( (echo $((08)); echo in) 2>/dev/null )
assert_eq "" "$out" "invalid octal constant: fatal"
out=$( (echo $((5 /)); echo in) 2>/dev/null )
assert_eq "" "$out" "operator without a right operand: fatal"

# --- contexts -----------------------------------------------------------------
f() { echo $((1/0)); echo in_function; }
out=$( (f; echo after) 2>/dev/null )
assert_eq "" "$out" "inside a function: the shell exits"
out=$( (if echo $((1/0)); then echo yes; fi; echo after) 2>/dev/null )
assert_eq "" "$out" "in an if condition: the shell exits"
out=$( (while echo $((1/0)); do :; done; echo after) 2>/dev/null )
assert_eq "" "$out" "in a while condition: the shell exits"
out=$( ( (echo $((1/0)); echo in) | cat ) 2>/dev/null )
assert_eq "" "$out" "in a pipeline: the subshell exits"

# --- a subshell's error does not end its parent -------------------------------
( echo $((1/0)) ) 2>/dev/null
echo survived >/dev/null
assert_eq "survived" "$( ( echo $((1/0)) ) 2>/dev/null; echo survived )" "the parent of a failing subshell goes on"

# --- the shell under test itself, non-interactive and interactive -------------
out=$("$SH" -c 'echo $((1/0)); echo after' 2>/dev/null)
st=$?
assert_eq "" "$out" "non-interactive shell: nothing printed"
assert_eq "nonzero" "$(nz $st)" "non-interactive shell: exits non-zero"
# (An interactive shell goes on after the error: checked on a pty by
# test/interactive/prompt_pty.py, since "-i" on a pipe is not interactive
# enough for dash and bash to survive the error.)

finish_tests
