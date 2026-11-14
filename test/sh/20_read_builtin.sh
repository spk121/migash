#!/bin/sh
# Test: the read utility (POSIX XCU "read")
# Covers: one line into one or more variables, IFS field splitting with the
#         remainder in the last variable, IFS unset/empty/non-whitespace,
#         backslash handling with and without -r, line continuation, exit
#         status at end of file and on errors, reading exactly one line from a
#         shared input, here-documents and "while read" loops.
#
# Notes:
#   - Inputs are produced with printf, never echo, so backslashes are literal.
#   - read is run in { } groups inside pipelines: the group is the last element
#     of the pipeline and runs in a subshell, where the variables are visible.
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the read utility...\n"

WORK="${TMPDIR:-/tmp}/read_test_$$"
mkdir -p "$WORK"

# -----------------------------
# 1. One line into variables
# -----------------------------
result=$(printf 'hello world\n' | { read x; printf '%s' "$x"; })
assert_eq "hello world" "$result" "read puts the line in the variable"

result=$(printf 'first second\n' | { read x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq "[first][second]" "$result" "fields go to successive variables"

result=$(printf 'a b c d\n' | { read x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq "[a][b c d]" "$result" "the last variable gets the rest of the line"

result=$(printf ' a  b  c \n' | { read x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq "[a][b  c]" "$result" "leading and trailing IFS whitespace are removed, inner kept in the rest"

result=$(printf 'a\n' | { read x y z; printf '[%s][%s][%s]' "$x" "$y" "$z"; })
assert_eq "[a][][]" "$result" "variables without a field are set to empty"

result=$(printf '  spaced  \n' | { read x; printf '[%s]' "$x"; })
assert_eq "[spaced]" "$result" "a single variable loses leading and trailing IFS whitespace"

result=$(printf 'a\tb\n' | { read x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq "[a][b]" "$result" "tab is IFS whitespace"

result=$(printf '   \n' | { read x; printf 'st=%s[%s]' "$?" "$x"; })
assert_eq "st=0[]" "$result" "a line of only IFS whitespace gives an empty variable and status 0"

result=$(printf '\n' | { read x; printf 'st=%s[%s]' "$?" "$x"; })
assert_eq "st=0[]" "$result" "an empty line gives an empty variable and status 0"

# -----------------------------
# 2. IFS
# -----------------------------
result=$(printf 'x:y:z\n' | { IFS=: read a b; printf '[%s][%s]' "$a" "$b"; })
assert_eq "[x][y:z]" "$result" "IFS=: splits on colons; the rest keeps its colons"

result=$(printf 'a:b:\n' | { IFS=: read x y z; printf '[%s][%s][%s]' "$x" "$y" "$z"; })
assert_eq "[a][b][]" "$result" "a trailing non-whitespace IFS character ends an empty field"

result=$(printf ' a : b \n' | { IFS=' :' read x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq "[a][b]" "$result" "IFS with whitespace and a separator: whitespace around the separator is dropped"

result=$(printf '  lead and trail  \n' | { IFS= read a; printf '[%s]' "$a"; })
assert_eq "[  lead and trail  ]" "$result" "empty IFS: no splitting and no trimming"

result=$(printf 'a b\n' | { IFS= read x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq "[a b][]" "$result" "empty IFS: the whole line goes to the first variable"

result=$(printf 'p,q,r\n' | { IFS=, read x y z; printf '[%s][%s][%s]' "$x" "$y" "$z"; })
assert_eq "[p][q][r]" "$result" "IFS=, splits on commas"

# -----------------------------
# 3. Backslash: escape without -r, literal with -r
# -----------------------------
result=$(printf 'a\\b\n' | { read x; printf '%s' "$x"; })
assert_eq "ab" "$result" "backslash escapes the next character (removed)"

result=$(printf 'a\\b\n' | { read -r x; printf '%s' "$x"; })
assert_eq 'a\b' "$result" "-r keeps the backslash"

result=$(printf 'a\\\nb\n' | { read x; printf '%s' "$x"; })
assert_eq "ab" "$result" "backslash-newline continues the line"

result=$(printf 'a\\\nb\n' | { read -r x; printf '[%s]' "$x"; })
assert_eq '[a\]' "$result" "-r: backslash-newline is not a continuation"

result=$(printf 'a\\ b c\n' | { read x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq "[a b][c]" "$result" "an escaped IFS character does not split"

result=$(printf 'a\\ b c\n' | { read -r x y; printf '[%s][%s]' "$x" "$y"; })
assert_eq '[a\][b c]' "$result" "-r: the backslash is literal and the space splits"

result=$(printf 'tail\\\\\n' | { read -r x; printf '%s' "$x"; })
assert_eq 'tail\\' "$result" "-r keeps a trailing pair of backslashes"

result=$(printf 'v\n' | { read -- x; printf '%s' "$x"; })
assert_eq "v" "$result" "-- ends the options"

# -----------------------------
# 4. End of file and errors
# -----------------------------
result=$(printf 'abc' | { read x; printf 'st=%s[%s]' "$?" "$x"; })
assert_eq "st=1[abc]" "$result" "a last line without newline is assigned but the status is non-zero"

result=$(: | { read x; printf 'st=%s[%s]' "$?" "$x"; })
assert_eq "st=1[]" "$result" "end of file with nothing read: status 1, variable empty"

result=$(read x </dev/null; printf 'st=%s' "$?")
assert_eq "st=1" "$result" "reading from /dev/null gives status 1"

read x <&- 2>/dev/null
status=$?
if [ "$status" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "a closed standard input is an error"
else
    assert_eq "nonzero" "$status" "a closed standard input is an error"
fi

printf 'v\n' | { read 1bad 2>/dev/null; status=$?; [ "$status" -ne 0 ] && echo nonzero || echo "$status"; } > "$WORK/st"
assert_eq "nonzero" "$(cat "$WORK/st")" "an invalid variable name is an error"

printf 'v\n' | { readonly ro_read_var=1; read ro_read_var 2>/dev/null; status=$?; [ "$status" -ne 0 ] && echo nonzero || echo "$status"; } > "$WORK/st"
assert_eq "nonzero" "$(cat "$WORK/st")" "assigning a readonly variable is an error"

# -----------------------------
# 5. Exactly one line is consumed from a shared input
# -----------------------------
result=$(printf 'l1\nl2\nl3\n' | { read a; read b; rest=$(cat); printf '%s|%s|%s' "$a" "$b" "$rest"; })
assert_eq "l1|l2|l3" "$result" "each read consumes one line; the rest is left for the next command"

result=$(printf 'first\nsecond\n' | { read a; cat; })
assert_eq "second" "$result" "read does not read ahead past the newline"

# -----------------------------
# 6. Here-documents and files
# -----------------------------
read h1 h2 <<EOF
foo bar
EOF
assert_eq "[foo][bar]" "[$h1][$h2]" "read from a here-document sets variables in the current shell"

printf '1\n2\n3\n' > "$WORK/lines.txt"
result=$(while read line; do printf '<%s>' "$line"; done < "$WORK/lines.txt")
assert_eq "<1><2><3>" "$result" "while read loop over a file"

printf 'x\ny' > "$WORK/nonl.txt"
result=$(while read line; do printf '<%s>' "$line"; done < "$WORK/nonl.txt")
assert_eq "<x>" "$result" "a final line without newline ends the loop (status 1)"

result=$(while read line || [ -n "$line" ]; do printf '<%s>' "$line"; done < "$WORK/nonl.txt")
assert_eq "<x><y>" "$result" "the || [ -n ] idiom still processes the final unterminated line"

result=$(printf 'k=v\n' | { IFS== read key value; printf '[%s][%s]' "$key" "$value"; })
assert_eq "[k][v]" "$result" "IFS== splits a key=value line"

result=$(printf 'one\n' | { read x; printf 'st=%s' "$?"; })
assert_eq "st=0" "$result" "status is 0 after a complete line"

rm -rf "$WORK"
finish_tests
