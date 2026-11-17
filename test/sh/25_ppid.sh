#!/bin/sh
# Test: the PPID variable (POSIX XCU 2.5.3)
# Covers: PPID is a decimal number; a child shell's PPID is the parent
#         shell's $$; a subshell and a command substitution keep the value;
#         the value inherited from the environment is replaced; PPID is not
#         exported by default.
#
# Notes:
#   - Whether PPID is read-only is not specified (bash: yes, dash: no) and
#     is not tested; the environment check accepts a read-only error.
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the PPID variable...\n"

SH="${TEST_SHELL:-sh}"
WORK="${TMPDIR:-/tmp}/ppid_test_$$"
mkdir -p "$WORK"

case "$PPID" in
    ''|*[!0-9]*) assert_eq "a decimal number" "'$PPID'" "PPID is a decimal number" ;;
    0) assert_eq "non-zero" "0" "PPID is a decimal number" ;;
    *) assert_eq "ok" "ok" "PPID is a decimal number" ;;
esac

# a child shell run in the foreground (not in a command substitution, which
# would fork first) has this shell as its parent
"$SH" -c 'echo "$PPID"' > "$WORK/child"
read -r child < "$WORK/child"
assert_eq "$$" "$child" "a child shell's PPID is the parent shell's \$\$"

assert_eq "$PPID" "$( echo "$PPID" )" "a command substitution keeps PPID"
( echo "$PPID" > "$WORK/sub" )
read -r sub < "$WORK/sub"
assert_eq "$PPID" "$sub" "a subshell keeps PPID"
f() { echo "$PPID"; }
assert_eq "$PPID" "$(f)" "a function sees the same PPID"

# the value inherited from the environment is replaced at startup
out=$(PPID=7 "$SH" -c 'echo "$PPID"' 2>/dev/null)
case "$out" in
    7) assert_eq "not 7" "7" "an inherited PPID is replaced at initialization" ;;
    *) assert_eq "ok" "ok" "an inherited PPID is replaced at initialization" ;;
esac

# not exported by default
exported=$("$SH" -c 'env | grep -c "^PPID="')
assert_eq "0" "$exported" "PPID is not exported by default"

rm -rf "$WORK"
finish_tests
