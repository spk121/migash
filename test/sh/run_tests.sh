#!/bin/sh
# POSIX Shell Quoting Test Suite
# Tests compliance with POSIX.1-2024 Section 2.2 (Quoting)
#
# Usage: ./run_tests.sh [shell_to_test]
# Example: ./run_tests.sh /bin/dash
#          ./run_tests.sh /bin/bash --posix

set -e

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# Default shell to test: first argument, else $MIGASH (set by "make check"),
# else /bin/sh.
TEST_SHELL="${1:-${MIGASH:-/bin/sh}}"
shift 2>/dev/null || true
SHELL_ARGS="$*"
# Tests that need to start a child shell use this.
export TEST_SHELL

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_XFAILED=0
TESTS_XPASSED=0

# Get the directory where this script lives
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

printf "${BLUE}POSIX Shell Quoting Test Suite${NC}\n"
printf "Testing: %s %s\n" "$TEST_SHELL" "$SHELL_ARGS"
printf "========================================\n\n"

# Expected failures: test files listed (one basename per line, # comments
# allowed) in expected_failures.txt are allowed to fail. A failure of a
# listed file is reported as XFAIL and does not fail the run; a pass is
# reported as XPASS, a reminder to remove the entry. Set MIGASH_STRICT_XFAIL=1
# to make an XPASS fail the run. Only applied to the shell being developed
# (the default shell or $MIGASH), never to a reference shell given by path.
XFAIL_FILE="$SCRIPT_DIR/expected_failures.txt"
is_expected_failure() {
    [ -f "$XFAIL_FILE" ] || return 1
    case "$TEST_SHELL" in
        */migash|migash|*/migash.exe) ;;
        *) return 1 ;;
    esac
    grep -v '^[[:space:]]*#' "$XFAIL_FILE" | grep -qx "$1"
}

# Run a single test file
run_test_file() {
    test_file="$1"
    test_name="$(basename "$test_file" .sh)"

    printf "${YELLOW}Running: %s${NC}\n" "$test_name"

    if "$TEST_SHELL" $SHELL_ARGS "$test_file"; then
        if is_expected_failure "$(basename "$test_file")"; then
            printf "${YELLOW}  XPASS (expected to fail; remove it from expected_failures.txt)${NC}\n\n"
            TESTS_XPASSED=$((TESTS_XPASSED + 1))
        else
            printf "${GREEN}  PASSED${NC}\n\n"
        fi
        TESTS_PASSED=$((TESTS_PASSED + 1))
    elif is_expected_failure "$(basename "$test_file")"; then
        printf "${YELLOW}  XFAIL (expected failure)${NC}\n\n"
        TESTS_XFAILED=$((TESTS_XFAILED + 1))
    else
        printf "${RED}  FAILED${NC}\n\n"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_RUN=$((TESTS_RUN + 1))
}

# Run all test files in the tests directory
for test_file in "$SCRIPT_DIR"/[0-9]*.sh; do
    if [ -f "$test_file" ]; then
        run_test_file "$test_file"
    fi
done

# Summary
printf "========================================\n"
printf "Results: %d/%d tests passed\n" "$TESTS_PASSED" "$TESTS_RUN"
if [ "$TESTS_XFAILED" -gt 0 ]; then
    printf "${YELLOW}%d expected failure(s)${NC}\n" "$TESTS_XFAILED"
fi
if [ "$TESTS_XPASSED" -gt 0 ]; then
    printf "${YELLOW}%d unexpected pass(es): update expected_failures.txt${NC}\n" "$TESTS_XPASSED"
fi

if [ "$TESTS_FAILED" -gt 0 ]; then
    printf "${RED}%d tests FAILED${NC}\n" "$TESTS_FAILED"
    exit 1
elif [ "$TESTS_XPASSED" -gt 0 ] && [ "${MIGASH_STRICT_XFAIL:-0}" = 1 ]; then
    exit 1
else
    printf "${GREEN}All tests PASSED${NC}\n"
    exit 0
fi
