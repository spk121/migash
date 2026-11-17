#!/bin/sh
# Test: the LINENO variable (POSIX XBD 8.1)
# Covers: the line within a script, counting blank lines, inside a function
#         body (the body's own lines), inside a loop body, for a command
#         spread over several lines (its first line), in if/then, for an
#         assignment-only command; a -c string; a sourced file (numbered from
#         1, the caller's numbering resumes afterwards); standard input.
#
# Notes:
#   - What happens after "unset LINENO" or "LINENO=n" is unspecified
#     ("may lose its special meaning") and is not tested.
#   - Checked against bash --posix; the dash at hand has no LINENO.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the LINENO variable...\n"

SH="${TEST_SHELL:-sh}"
WORK="${TMPDIR:-/tmp}/lineno_test_$$"
mkdir -p "$WORK"

cat > "$WORK/script.sh" <<'EOF'
echo "l1=$LINENO"

echo "l3=$LINENO"
f() {
    echo "fn5=$LINENO"
    echo "fn6=$LINENO"
}
f
for i in 1; do
    echo "loop10=$LINENO"
    echo "loop11=$LINENO"
done
echo "multi=$LINENO" \
    "cont=$LINENO"
if true
then echo "then16=$LINENO"
fi
x=$LINENO; echo "assign18=$x"
echo "last=$LINENO"
EOF
out=$("$SH" "$WORK/script.sh" | tr '\n' ' ')
assert_eq "l1=1 l3=3 fn5=5 fn6=6 loop10=10 loop11=11 multi=13 cont=13 then16=16 assign18=18 last=19 " \
    "$out" "line numbers within a script"

# the pieces again, one at a time, for a readable failure
"$SH" "$WORK/script.sh" > "$WORK/out"
line() { sed -n "${1}p" "$WORK/out"; }
assert_eq "l1=1" "$(line 1)" "the first line is 1"
assert_eq "l3=3" "$(line 2)" "blank lines count"
assert_eq "fn5=5" "$(line 3)" "a function body keeps the script's lines"
assert_eq "loop11=11" "$(line 6)" "a loop body keeps the script's lines"
assert_eq "multi=13 cont=13" "$(line 7)" "a command over several lines reports its first line"
assert_eq "then16=16" "$(line 8)" "the command after then"
assert_eq "assign18=18" "$(line 9)" "an assignment sees the line"

printf 'echo "src1=$LINENO"\necho "src2=$LINENO"\n' > "$WORK/sourced.sh"
out=$("$SH" -c 'echo "c1=$LINENO"
echo "c2=$LINENO"; . "$1"; echo "after=$LINENO"
echo "c3=$LINENO"' sh "$WORK/sourced.sh" | tr '\n' ' ')
assert_eq "c1=1 c2=2 src1=1 src2=2 after=2 c3=3 " "$out" "-c string and a sourced file"

out=$(printf 'echo "i1=$LINENO"\n\necho "i3=$LINENO"\n' | "$SH" | tr '\n' ' ')
assert_eq "i1=1 i3=3 " "$out" "commands from standard input"

out=$("$SH" -c '( echo "sub=$LINENO" ); echo "$( echo "cs=$LINENO" )"' | tr '\n' ' ')
assert_eq "sub=1 cs=1 " "$out" "subshell and command substitution"

f() {
    echo "$LINENO"
}
assert_eq "$(( $(grep -n 'echo "\$LINENO"' "$0" | head -1 | cut -d: -f1) ))" "$(f)" \
    "a function in this test file reports its body's line"

rm -rf "$WORK"
finish_tests
