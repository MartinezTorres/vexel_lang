#!/usr/bin/env bash
set -uo pipefail

# Test runner for lexer edge cases
# Tests that should FAIL with specific error messages

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
COMPILER="${VEXEL_FRONTEND:-$ROOT/build/vexel-frontend}"
PASS=0
FAIL=0

test_should_fail() {
    local testfile=$1
    local expected_error=$2
    local testname=$(basename "$testfile" .vx)

    echo -n "Testing $testname... "

    if ! output=$("$COMPILER" "$SCRIPT_DIR/$testfile" 2>&1); then
        if echo "$output" | grep -q "$expected_error"; then
            echo "PASS (got expected error)"
            ((PASS++))
        else
            echo "FAIL (wrong error message)"
            echo "  Expected: $expected_error"
            echo "  Got: $output"
            ((FAIL++))
        fi
    else
        echo "FAIL (should have failed but succeeded)"
        ((FAIL++))
    fi
}

test_should_pass() {
    local testfile=$1
    local testname=$(basename "$testfile" .vx)

    echo -n "Testing $testname... "

    if output=$("$COMPILER" "$SCRIPT_DIR/$testfile" 2>&1); then
        echo "PASS"
        ((PASS++))
    else
        echo "FAIL (should have succeeded)"
        echo "  Got: $output"
        ((FAIL++))
    fi
}

echo "=== Lexer Edge Case Tests ==="
echo

# Tests that should fail
test_should_fail "test_unterminated_string.vx" "Unterminated string"
test_should_fail "test_unterminated_char.vx" "Unterminated char literal"
test_should_fail "test_empty_char.vx" "Empty character literal"
test_should_fail "test_empty_hex.vx" "Invalid hexadecimal literal"
test_should_fail "test_escape_eof.vx" "Unterminated escape sequence"
test_should_fail "test_hex_escape_eof.vx" "Unterminated hex escape sequence"
test_should_fail "test_char_escape_eof.vx" "Unterminated"

# Tests that should pass
test_should_pass "test_valid_hex.vx"
test_should_pass "test_valid_escapes.vx"
test_should_pass "test_comment_eof.vx"
test_should_pass "test_whitespace_only.vx"

echo
echo "=== Results ==="
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo

if [ $FAIL -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed"
    exit 1
fi
