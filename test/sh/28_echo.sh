#!/bin/sh
# Test: the echo utility (POSIX XCU "echo", XSI)
# Covers: no options and no "--" (every argument is printed, including
#         "-n", "-e", "-E" and "--"); the XSI backslash escapes in every
#         argument (\a \b \f \n \r \t \v \\ \0num) and \c, which ends the
#         output without the trailing newline; an unknown escape is kept;
#         no arguments and an empty argument print a newline.
#
# Notes:
#   - This is the XSI echo that POSIX describes, which migash follows by
#     decision: "-n" is an operand, not an option. dash agrees on everything
#     except "-n", which it takes as an option; bash --posix takes options
#     and does not interpret escapes without xpg_echo. The escape cases
#     were checked against dash.
#   - Output is captured through od so that control characters are visible.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the echo utility (XSI)...\n"

# octal dump of the output, as a single line
dump() { od -An -to1 | tr -s ' \n' ' ' | sed 's/^ //; s/ $//'; }

assert_eq "a b" "$(echo a b)" "arguments are joined with spaces"
assert_eq "" "$(echo)" "no arguments: an empty line"
assert_eq "012" "$(echo | dump)" "no arguments: the newline is written"
assert_eq "012" "$(echo "" | dump)" "an empty argument: the newline is written"

# --- no options, no "--" ---------------------------------------------------
assert_eq "-n a" "$(echo -n a)" "-n is an operand"
assert_eq "055 156 012" "$(echo -n | dump)" "echo -n prints -n and a newline"
assert_eq "-e a" "$(echo -e a)" "-e is an operand"
assert_eq "-E a" "$(echo -E a)" "-E is an operand"
assert_eq "-- a" "$(echo -- a)" "-- is an operand"
assert_eq "-x -y" "$(echo -x -y)" "any dash word is an operand"
assert_eq "0" "$( echo -z >/dev/null 2>&1; echo $? )" "no option is an error"

# --- escapes in every argument ----------------------------------------------
assert_eq "141 011 142 012" "$(echo 'a\tb' | dump)" "\\t is a tab"
assert_eq "141 012 142 012" "$(echo 'a\nb' | dump)" "\\n is a newline"
assert_eq "141 134 142 012" "$(echo 'a\\b' | dump)" "\\\\ is a backslash"
assert_eq "007 010 014 015 013 012" "$(echo '\a\b\f\r\v' | dump)" "\\a \\b \\f \\r \\v"
assert_eq "101 012" "$(echo '\0101' | dump)" "\\0num is an octal byte"
assert_eq "101 070 012" "$(echo '\01018' | dump)" "\\0num takes at most three digits"
assert_eq "000 012" "$(echo '\0' | dump)" "\\0 alone is a NUL byte"
assert_eq "141 011 142 040 143 011 144 012" "$(echo 'a\tb' 'c\td' | dump)" "escapes are recognised in every argument"
assert_eq "141 134 161 012" "$(echo 'a\q' | dump)" "an unknown escape is kept as written"
assert_eq "134 012" "$(echo '\' | dump)" "a trailing backslash is kept"

# --- \c ends the output -----------------------------------------------------
assert_eq "141" "$(echo 'a\cb' | dump)" "\\c ends the output, no newline"
assert_eq "141 040" "$(echo a '\c' b | dump)" "\\c in a later argument ends the output there"
assert_eq "ab" "$(echo 'a\c'; echo b)" "after \\c the next command starts on the same line"

finish_tests
