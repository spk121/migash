#!/bin/sh
# Test: shell options (POSIX XCU 2.14 "set", 2.8.1 consequences of errors,
#       2.5.2 "$-") and "wait" with no operands
# Covers: -e (errexit) and the contexts where it is ignored, -u (nounset),
#         -f (noglob), -C (noclobber), -n (noexec), -x (xtrace), -v (verbose),
#         -a (allexport), set -o / +o listing and round trip, long option
#         names, combined flags, "$-", and the exit status after an -e exit.
#
# Notes:
#   - Options that end the shell (-e, -u, -n) are exercised in a child shell:
#     the runner exports TEST_SHELL; run directly, the script falls back to sh.
#   - Expected values were checked against dash and bash --posix.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing shell options (set -e -u -f -C -n -x -v -a -o)...\n"

SH="${TEST_SHELL:-sh}"
WORK="${TMPDIR:-/tmp}/set_options_test_$$"
mkdir -p "$WORK"

# -----------------------------
# 1. errexit: a failing simple command ends the shell
# -----------------------------
result=$($SH -c 'set -e; false; echo survived' 2>/dev/null)
assert_eq "" "$result" "set -e: a failing command ends the shell"

$SH -c 'set -e; false; echo survived' >/dev/null 2>&1
assert_eq "1" "$?" "set -e: the shell exits with the failing command's status"

$SH -c 'set -e; (exit 3); echo survived' >/dev/null 2>&1
assert_eq "3" "$?" "set -e: a failing subshell command ends the shell with its status"

result=$($SH -c 'set -e; x=$(false); echo survived' 2>/dev/null)
assert_eq "" "$result" "set -e: an assignment whose command substitution fails ends the shell"

result=$($SH -c 'set -e; true | false; echo survived' 2>/dev/null)
assert_eq "" "$result" "set -e: a pipeline whose last command fails ends the shell"

result=$($SH -c 'set -e; false | true; echo survived' 2>/dev/null)
assert_eq "survived" "$result" "set -e: a pipeline whose last command succeeds continues"

result=$($SH -c 'set -e; f() { false; echo in-f; }; f; echo after' 2>/dev/null)
assert_eq "" "$result" "set -e: a failure inside a function ends the shell"

result=$($SH -c 'set -e; (false; echo in-subshell); echo after' 2>/dev/null)
assert_eq "" "$result" "set -e: a subshell inherits -e and its failure ends the parent"

result=$($SH -c 'trap "echo exit-trap" EXIT; set -e; false; echo survived' 2>/dev/null)
assert_eq "exit-trap" "$result" "set -e: the EXIT trap runs when -e ends the shell"

# -----------------------------
# 2. errexit is ignored in these contexts (XCU 2.14 set, -e)
# -----------------------------
result=$($SH -c 'set -e; false || echo rescued' 2>/dev/null)
assert_eq "rescued" "$result" "set -e ignored: left side of ||"

result=$($SH -c 'set -e; false && echo no; echo after' 2>/dev/null)
assert_eq "after" "$result" "set -e ignored: failing left side of &&"

result=$($SH -c 'set -e; ! true; echo after' 2>/dev/null)
assert_eq "after" "$result" "set -e ignored: pipeline beginning with !"

result=$($SH -c 'set -e; if false; then :; fi; echo after' 2>/dev/null)
assert_eq "after" "$result" "set -e ignored: if condition"

result=$($SH -c 'set -e; while false; do :; done; echo after' 2>/dev/null)
assert_eq "after" "$result" "set -e ignored: while condition"

result=$($SH -c 'set -e; until true; do :; done; echo after' 2>/dev/null)
assert_eq "after" "$result" "set -e ignored: until condition"

result=$($SH -c 'set -e; f() { false; echo in-f; }; f || echo rescued; echo after' 2>/dev/null)
assert_eq "in-f
after" "$result" "set -e ignored: inside a function called where -e is ignored"

result=$($SH -c 'set -e; if false; then :; else echo else-ran; fi' 2>/dev/null)
assert_eq "else-ran" "$result" "set -e: a false if condition runs the else branch"

result=$($SH -c 'set -e; set +e; false; echo back' 2>/dev/null)
assert_eq "back" "$result" "set +e turns errexit off again"

# -----------------------------
# 3. nounset
# -----------------------------
$SH -c 'set -u; echo "$undefined_variable_zz"; echo survived' >/dev/null 2>&1
if [ "$?" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "set -u: expanding an unset variable is an error"
else
    assert_eq "nonzero" "0" "set -u: expanding an unset variable is an error"
fi

result=$($SH -c 'set -u; echo "$undefined_variable_zz"; echo survived' 2>/dev/null)
assert_eq "" "$result" "set -u: the error ends a non-interactive shell"

result=$($SH -c 'set -u; empty=; echo "[$empty]"' 2>/dev/null)
assert_eq "[]" "$result" "set -u: an empty variable is set"

result=$($SH -c 'set -u; echo "${undefined_variable_zz:-default}"' 2>/dev/null)
assert_eq "default" "$result" "set -u: \${var:-word} is allowed for an unset variable"

result=$($SH -c 'set -u; echo "${undefined_variable_zz-word}"' 2>/dev/null)
assert_eq "word" "$result" "set -u: \${var-word} is allowed for an unset variable"

result=$($SH -c 'set -u; echo "[$#]" "[$*]" "[$@]"' 2>/dev/null)
assert_eq "[0] [] []" "$result" "set -u: \$@ and \$* are exempt with no positional parameters"

result=$($SH -c 'set -u; echo "$1"; echo survived' 2>/dev/null)
assert_eq "" "$result" "set -u: an unset positional parameter is an error"

result=$($SH -c 'set -u; set +u; echo "[$undefined_variable_zz]"' 2>/dev/null)
assert_eq "[]" "$result" "set +u turns nounset off again"

# -----------------------------
# 4. noglob
# -----------------------------
touch "$WORK/g1.tmp" "$WORK/g2.tmp"
result=$(cd "$WORK" && set -f && echo *.tmp)
assert_eq "*.tmp" "$result" "set -f: pathname expansion is off"
result=$(cd "$WORK" && set -f && set +f && echo *.tmp)
assert_eq "g1.tmp g2.tmp" "$result" "set +f: pathname expansion is back on"

# -----------------------------
# 5. noclobber
# -----------------------------
result=$(
    cd "$WORK" && set -C
    echo first > clob.txt
    echo second > clob.txt 2>/dev/null || echo blocked
    cat clob.txt
)
assert_eq "blocked
first" "$result" "set -C: > refuses to overwrite an existing file"

result=$(cd "$WORK" && set -C && echo forced >| clob.txt && cat clob.txt)
assert_eq "forced" "$result" "set -C: >| overwrites anyway"

result=$(cd "$WORK" && set -C && echo more >> clob.txt && cat clob.txt)
assert_eq "forced
more" "$result" "set -C: >> appends regardless"

result=$(set -C && echo devnull > /dev/null && echo ok)
assert_eq "ok" "$result" "set -C: writing to a device file is allowed"

result=$(cd "$WORK" && set -C && rm -f new.txt && echo created > new.txt && cat new.txt)
assert_eq "created" "$result" "set -C: creating a new file is allowed"

# -----------------------------
# 6. noexec, xtrace, verbose
# -----------------------------
result=$($SH -n -c 'echo should-not-run' 2>&1)
assert_eq "" "$result" "-n: commands are read but not executed"

result=$($SH -c 'echo before; set -n; echo not-run' 2>/dev/null)
assert_eq "before" "$result" "set -n: nothing after it is executed"

result=$($SH -c 'set -x; echo hi' 2>&1 >/dev/null)
assert_eq "+ echo hi" "$result" "set -x: the expanded command is traced to stderr with PS4"

result=$($SH -c 'set -x; v=val; echo $v' 2>&1 >/dev/null)
assert_eq "+ v=val
+ echo val" "$result" "set -x: traces assignments and expanded words"

result=$($SH -c 'set -x; PS4="T> "; echo hi' 2>&1 >/dev/null | tail -1)
assert_eq "T> echo hi" "$result" "set -x: PS4 is the trace prefix"

# -v echoes input lines as they are read, so it needs a script read line by
# line (a -c string has already been read when set -v runs).
printf 'set -v\necho hi\n' > "$WORK/verbose.sh"
result=$($SH "$WORK/verbose.sh" 2>&1 >/dev/null)
case $result in
    *"echo hi"*) assert_eq "ok" "ok" "set -v: input lines are echoed to stderr" ;;
    *) assert_eq "echo hi" "$result" "set -v: input lines are echoed to stderr" ;;
esac

# -----------------------------
# 7. allexport
# -----------------------------
result=$($SH -c 'set -a; AX_EXPORTED=yes; sh -c "echo \${AX_EXPORTED:-unset}"' 2>/dev/null)
assert_eq "yes" "$result" "set -a: assigned variables are exported"

result=$($SH -c 'set -a; set +a; AX_NOT=yes; sh -c "echo \${AX_NOT:-unset}"' 2>/dev/null)
assert_eq "unset" "$result" "set +a: later assignments are not exported"

# -----------------------------
# 8. $- reflects the options
# -----------------------------
result=$($SH -c 'set -e; case $- in *e*) echo yes;; *) echo no;; esac')
assert_eq "yes" "$result" "\$- contains e after set -e"

result=$($SH -c 'set -u; set +u; case $- in *u*) echo yes;; *) echo no;; esac')
assert_eq "no" "$result" "\$- drops u after set +u"

result=$($SH -c 'set -fC; case $- in *f*C*|*C*f*) echo both;; *) echo missing;; esac')
assert_eq "both" "$result" "set -fC sets several options at once"

result=$($SH -c 'set -fC; set +fC; case $- in *[fC]*) echo still;; *) echo cleared;; esac')
assert_eq "cleared" "$result" "set +fC clears several options at once"

# -----------------------------
# 9. set -o / +o
# -----------------------------
result=$($SH -c 'set -o errexit; case $- in *e*) echo on;; *) echo off;; esac')
assert_eq "on" "$result" "set -o errexit is set -e"

result=$($SH -c 'set -o nounset; set +o nounset; case $- in *u*) echo on;; *) echo off;; esac')
assert_eq "off" "$result" "set +o nounset is set +u"

result=$($SH -c 'set -o noglob -o noclobber; case $- in *f*) case $- in *C*) echo both;; esac;; esac')
assert_eq "both" "$result" "several -o options in one command"

count=$($SH -c 'set -o' 2>/dev/null | grep -c 'errexit')
assert_eq "1" "$count" "set -o lists the options (one line per option)"

count=$($SH -c 'set -o' 2>/dev/null | grep -c -E '^(allexport|noclobber|noglob|nounset|xtrace|verbose|noexec|errexit) ')
assert_eq "8" "$count" "set -o lists all the standard options"

count=$($SH -c 'set +o' 2>/dev/null | grep -c '^set [-+]o errexit$')
assert_eq "1" "$count" "set +o prints set commands that reproduce the options"

result=$($SH -c 'saved=$(set +o); set -ef; eval "$saved"; case $- in *[ef]*) echo not-restored;; *) echo restored;; esac')
assert_eq "restored" "$result" "set +o output restores the options through eval"

$SH -c 'set -o nosuchoption_zz' >/dev/null 2>&1
if [ "$?" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "set -o with an unknown option fails"
else
    assert_eq "nonzero" "0" "set -o with an unknown option fails"
fi

$SH -c 'set -Q' >/dev/null 2>&1
if [ "$?" -ne 0 ]; then
    assert_eq "nonzero" "nonzero" "set with an unknown flag fails"
else
    assert_eq "nonzero" "0" "set with an unknown flag fails"
fi

# -----------------------------
# 10. set and the positional parameters still work alongside options
# -----------------------------
result=$($SH -c 'set -f -- a "*" c; echo "$#:$2"; set +f')
assert_eq "3:*" "$result" "options and -- operands in one set command"

result=$($SH -c 'set -e; set -- x y; echo "$#"')
assert_eq "2" "$result" "set -- after set -e keeps working"

# -----------------------------
# 11. wait with no operands waits for every background job
# -----------------------------
result=$( (sleep 1; echo child) & wait; echo parent )
assert_eq "child
parent" "$result" "wait with no operands waits for background jobs"

# -----------------------------
# 12. pipefail: the status of the rightmost failing command
# -----------------------------
result=$($SH -c 'false | true; echo $?')
assert_eq "0" "$result" "without pipefail a pipeline has its last command's status"

result=$($SH -c 'set -o pipefail; false | true; echo $?')
assert_eq "1" "$result" "pipefail: a failing first command fails the pipeline"

result=$($SH -c 'set -o pipefail; (exit 3) | (exit 4) | true; echo $?')
assert_eq "4" "$result" "pipefail: the rightmost failing command gives the status"

result=$($SH -c 'set -o pipefail; true | true; echo $?')
assert_eq "0" "$result" "pipefail: a pipeline of successes succeeds"

result=$($SH -c 'set -o pipefail; ! false | true; echo $?')
assert_eq "0" "$result" "pipefail: ! negates the pipefail status"

result=$($SH -c 'set -o pipefail; set +o pipefail; (exit 3) | true; echo $?')
assert_eq "0" "$result" "set +o pipefail turns it off again"

rm -rf "$WORK"
finish_tests
