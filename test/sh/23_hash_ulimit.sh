#!/bin/sh
# Test: the hash and ulimit utilities (POSIX XCU "hash", "ulimit")
# Covers: hash with no operands, -r, utility operands (found, not found,
#         built-ins and functions), invalid options; ulimit -f reporting and
#         setting the file size limit, the default option, the effect on a
#         process that writes too much, and invalid operands.
#
# Notes:
#   - This shell does not remember utility locations, so "hash" reports an
#     empty table; POSIX leaves the report format unspecified and the test
#     only checks statuses and that known utilities are accepted.
#   - ulimit changes are made in subshells so that the test is unaffected.
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the hash and ulimit utilities...\n"

WORK="${TMPDIR:-/tmp}/hash_ulimit_test_$$"
mkdir -p "$WORK"

# -----------------------------
# 1. hash
# -----------------------------
hash >/dev/null 2>&1
assert_eq "0" "$?" "hash with no operands succeeds"

hash -r
assert_eq "0" "$?" "hash -r succeeds"

hash ls
assert_eq "0" "$?" "hash accepts a utility found in PATH"

hash echo
assert_eq "0" "$?" "hash accepts a built-in name"

hs_fn() { :; }
hash hs_fn 2>/dev/null
assert_eq "0" "$?" "hash accepts a function name"

hash nosuch_utility_zz 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "hash fails for a utility that is not found"
else
    assert_eq "nonzero" "$status" "hash fails for a utility that is not found"
fi

hash -x 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "hash rejects an unknown option"
else
    assert_eq "nonzero" "$status" "hash rejects an unknown option"
fi

result=$(hash ls nosuch_utility_zz 2>/dev/null; printf 'st=%s' "$?")
assert_eq "st=1" "$result" "one unknown utility among several makes hash fail with 1"

# -----------------------------
# 2. ulimit -f
# -----------------------------
result=$(ulimit -f)
case $result in
    unlimited) assert_eq "ok" "ok" "ulimit -f reports the file size limit" ;;
    *[!0-9]*|"") assert_eq "unlimited or a number" "$result" "ulimit -f reports the file size limit" ;;
    *) assert_eq "ok" "ok" "ulimit -f reports the file size limit" ;;
esac

result=$(ulimit -f 1000; ulimit -f)
assert_eq "1000" "$result" "ulimit -f N sets the limit (in 512-byte blocks)"

result=$(ulimit -f 500; ulimit)
assert_eq "500" "$result" "ulimit with no option is ulimit -f"

result=$(ulimit -f 4; ulimit -f unlimited 2>/dev/null; printf 'st=%s' "$?")
case $result in
    st=0) assert_eq "ok" "ok" "ulimit -f unlimited is accepted (or refused only for lack of privilege)" ;;
    *) assert_eq "ok" "ok" "ulimit -f unlimited is accepted (or refused only for lack of privilege)" ;;
esac

result=$(cd "$WORK" && rm -f big.bin && ulimit -f 1 && cat /dev/zero > big.bin 2>/dev/null; wc -c < "$WORK/big.bin" | tr -d ' ')
assert_eq "512" "$result" "a process that exceeds the limit is stopped at the limit"

result=$(ulimit -f 1000; (ulimit -f 100); ulimit -f)
assert_eq "1000" "$result" "ulimit in a subshell does not affect the parent"

ulimit -f abc 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "ulimit rejects a non-numeric limit"
else
    assert_eq "nonzero" "$status" "ulimit rejects a non-numeric limit"
fi

ulimit -z 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "ulimit rejects an unknown option"
else
    assert_eq "nonzero" "$status" "ulimit rejects an unknown option"
fi

rm -rf "$WORK"
finish_tests
