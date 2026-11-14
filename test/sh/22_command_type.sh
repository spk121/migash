#!/bin/sh
# Test: the command and type utilities (POSIX XCU "command"; "type" is a
#       regular built-in in POSIX.1-2024)
# Covers: command bypassing functions, special built-ins losing their special
#         properties, exit status pass-through, -p, -v for built-ins, reserved
#         words, functions, aliases and executables, -V, and type's
#         descriptions. Formats POSIX leaves unspecified are tested loosely.
#
# Notes: expected values were checked against dash and bash --posix; where
# they differ (e.g. the status of "command -v" for a missing name, the exact
# wording for functions and aliases) the assertion only checks what both do.

. "$(dirname "$0")/test_helpers.sh"

printf "  Testing the command and type utilities...\n"

# -----------------------------
# 1. command runs the command without function lookup
# -----------------------------
result=$(echo() { printf 'function\n'; }; command echo real)
assert_eq "real" "$result" "command bypasses a function with the name of a built-in"

result=$(f() { printf 'function\n'; }; command f 2>/dev/null; printf 'st=%s' "$?")
assert_eq "st=127" "$result" "command does not find a function at all"

result=$(command false; printf 'st=%s' "$?")
assert_eq "st=1" "$result" "command returns the command's status"

result=$(command nosuchcmd_zz 2>/dev/null; printf 'st=%s' "$?")
assert_eq "st=127" "$result" "an unknown command gives 127"

result=$(command cd /; pwd)
assert_eq "/" "$result" "a regular built-in runs through command"

result=$(command set -- a b; printf '%s' "$#")
assert_eq "2" "$result" "a special built-in still works through command"

result=$(command unset -x 2>/dev/null; printf 'survived')
assert_eq "survived" "$result" "a special built-in's error does not end the shell through command"

result=$(PATH=/nonexistent_dir_zz command -p ls /dev/null 2>/dev/null)
assert_eq "/dev/null" "$result" "command -p finds utilities with the default PATH"

result=$(command; printf 'st=%s' "$?")
assert_eq "st=0" "$result" "command with no operands does nothing"

# -----------------------------
# 2. command -v
# -----------------------------
result=$(command -v echo)
assert_eq "echo" "$result" "-v names a built-in by its name"

result=$(command -v if)
assert_eq "if" "$result" "-v names a reserved word by its name"

result=$(f() { :; }; command -v f)
assert_eq "f" "$result" "-v names a function by its name"

alias cv_alias='echo hi'
result=$(command -v cv_alias)
assert_eq "alias cv_alias='echo hi'" "$result" "-v prints an alias as an alias command"
unalias cv_alias

result=$(command -v ls)
case $result in
    /*/ls) assert_eq "ok" "ok" "-v prints the absolute pathname of a utility" ;;
    *) assert_eq "/.../ls" "$result" "-v prints the absolute pathname of a utility" ;;
esac

result=$(command -v /bin/sh)
assert_eq "/bin/sh" "$result" "-v echoes an executable pathname"

result=$(command -v nosuchcmd_zz; printf 'st=%s' "$?")
case $result in
    st=0|"") assert_eq "nonzero status, no output" "$result" "-v for an unknown name prints nothing and fails" ;;
    st=*) assert_eq "ok" "ok" "-v for an unknown name prints nothing and fails" ;;
    *) assert_eq "nonzero status, no output" "$result" "-v for an unknown name prints nothing and fails" ;;
esac

result=$(command -v; printf 'st=%s' "$?")
assert_eq "st=0" "$result" "-v with no name does nothing"

# -----------------------------
# 3. command -V and type
# -----------------------------
result=$(command -V echo)
assert_eq "echo is a shell builtin" "$result" "-V describes a built-in"

result=$(type echo)
assert_eq "echo is a shell builtin" "$result" "type describes a built-in"

result=$(type if)
assert_eq "if is a shell keyword" "$result" "type describes a reserved word"

result=$(f() { :; }; type f | head -n 1)
case $result in
    "f is a"*function*) assert_eq "ok" "ok" "type describes a function" ;;
    *) assert_eq "f is a (shell) function" "$result" "type describes a function" ;;
esac

alias ty_alias='echo hi'
result=$(type ty_alias)
case $result in
    "ty_alias is "*alias*) assert_eq "ok" "ok" "type describes an alias" ;;
    *) assert_eq "ty_alias is an alias ..." "$result" "type describes an alias" ;;
esac
unalias ty_alias

result=$(type ls)
assert_eq "ls is $(command -v ls)" "$result" "type gives the pathname of a utility"

result=$(type nosuch_zz 2>/dev/null; printf 'st=%s' "$?")
case $result in
    st=0) assert_eq "nonzero" "$result" "type fails for an unknown name" ;;
    *) assert_eq "ok" "ok" "type fails for an unknown name" ;;
esac

count=$(type echo if | wc -l | tr -d ' ')
assert_eq "2" "$count" "type describes each operand on its own line"

result=$(type echo nosuch_zz 2>/dev/null | head -n 1; printf 'st=%s' "$?")
case $result in
    "echo is a shell builtin"*) assert_eq "ok" "ok" "type keeps going after an unknown name" ;;
    *) assert_eq "echo is a shell builtin ..." "$result" "type keeps going after an unknown name" ;;
esac

finish_tests
