#!/bin/sh
# Test: the umask utility (POSIX XCU "umask")
# Covers: printing the mask in octal and symbolic (-S) form, setting it from
#         an octal number and from symbolic clauses (=, +, -, who lists), the
#         effect on files created by utilities and by redirections, subshell
#         isolation, and the status for invalid operands.
#
# Notes:
#   - Every umask change is made in a subshell so that the test's own mask
#     is untouched.
#   - Output of "umask -S" with an operand is left unspecified by POSIX and
#     differs between shells, so it is not tested.
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the umask utility...\n"

WORK="${TMPDIR:-/tmp}/umask_test_$$"
mkdir -p "$WORK"

# -----------------------------
# 1. Printing
# -----------------------------
result=$(umask 022; umask)
assert_eq "0022" "$result" "umask prints the mask in octal"

result=$(umask 077; umask)
assert_eq "0077" "$result" "umask 077"

result=$(umask 0; umask)
assert_eq "0000" "$result" "umask 0 clears the mask"

result=$(umask 022; umask -S)
assert_eq "u=rwx,g=rx,o=rx" "$result" "umask -S prints the permissions allowed"

result=$(umask 077; umask -S)
assert_eq "u=rwx,g=,o=" "$result" "umask -S with empty classes"

result=$(umask 0; umask -S)
assert_eq "u=rwx,g=rwx,o=rwx" "$result" "umask -S with nothing masked"

# -----------------------------
# 2. Octal operands
# -----------------------------
result=$(umask 077; umask 0022; printf 'st=%s ' "$?"; umask)
assert_eq "st=0 0022" "$result" "setting the mask succeeds with status 0"

result=$(umask 27; umask)
assert_eq "0027" "$result" "a short octal number is accepted"

result=$(umask 0777; umask)
assert_eq "0777" "$result" "umask 0777 masks everything"

# -----------------------------
# 3. Symbolic operands
# -----------------------------
result=$(umask u=rwx,g=rx,o=; umask)
assert_eq "0027" "$result" "symbolic assignment per class"

result=$(umask u=rwx,g=,o=; umask -S)
assert_eq "u=rwx,g=,o=" "$result" "symbolic assignment round-trips through -S"

result=$(umask a=rwx; umask)
assert_eq "0000" "$result" "a=rwx allows everything"

result=$(umask 0; umask go-w; umask)
assert_eq "0022" "$result" "go-w removes write permission from group and others"

result=$(umask 022; umask g+w; umask)
assert_eq "0002" "$result" "g+w adds write permission for the group"

result=$(umask 077; umask o+r; umask)
assert_eq "0073" "$result" "o+r adds read permission for others"

result=$(umask 0; umask a-rwx; umask)
assert_eq "0777" "$result" "a-rwx denies everything"

result=$(umask 077; umask u=rx,g=rx; umask)
assert_eq "0227" "$result" "several clauses in one operand; the unnamed class keeps its mask"

# -----------------------------
# 4. Effect on created files
# -----------------------------
result=$(cd "$WORK" && umask 077 && rm -f f1 && touch f1 && ls -l f1 | cut -c1-10)
assert_eq "-rw-------" "$result" "a utility creates files subject to the mask"

result=$(cd "$WORK" && umask 022 && rm -f f2 && : > f2 && ls -l f2 | cut -c1-10)
assert_eq "-rw-r--r--" "$result" "a redirection creates files subject to the mask"

result=$(cd "$WORK" && umask 027 && rm -f f3 && echo x > f3 && ls -l f3 | cut -c1-10)
assert_eq "-rw-r-----" "$result" "mask 027 on a redirected file"

result=$(cd "$WORK" && umask 077 && rm -rf d1 && mkdir d1 && ls -ld d1 | cut -c1-10)
assert_eq "drwx------" "$result" "a directory is subject to the mask"

# -----------------------------
# 5. Scope
# -----------------------------
result=$(umask 022; (umask 077); umask)
assert_eq "0022" "$result" "umask in a subshell does not affect the parent"

result=$(umask 022; f() { umask 077; }; f; umask)
assert_eq "0077" "$result" "umask in a function affects the shell"

# -----------------------------
# 6. Errors
# -----------------------------
(umask 999) 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "an octal number out of range is an error"
else
    assert_eq "nonzero" "$status" "an octal number out of range is an error"
fi

(umask xyz) 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "a malformed symbolic mask is an error"
else
    assert_eq "nonzero" "$status" "a malformed symbolic mask is an error"
fi

result=$(umask 022; umask 999 2>/dev/null; umask)
assert_eq "0022" "$result" "a rejected operand leaves the mask unchanged"

rm -rf "$WORK"
finish_tests
