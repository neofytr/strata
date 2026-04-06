#!/bin/bash
#
# run_tests.sh — Compile and run all neobuild test suites.
#
# Usage: cd <project_root> && bash tests/run_tests.sh
#

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

CC="${CC:-gcc}"
NEOBUILD_SRC="buildsysdep/neobuild.c"
NEOBUILD_OBJ="buildsysdep/neobuild.o"
TEST_DIR="tests"
BIN_DIR="/tmp/neo_test_bins"

echo "========================================"
echo " neobuild test runner"
echo "========================================"
echo ""
echo "Project root: $PROJECT_ROOT"
echo "Compiler:     $CC"
echo ""

# Create output directory for test binaries
mkdir -p "$BIN_DIR"

# Compile neobuild.o if needed or if source is newer
if [ ! -f "$NEOBUILD_OBJ" ] || [ "$NEOBUILD_SRC" -nt "$NEOBUILD_OBJ" ]; then
    echo "[BUILD] Compiling $NEOBUILD_SRC ..."
    $CC -c "$NEOBUILD_SRC" -o "$NEOBUILD_OBJ" -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
    echo "[BUILD] Done."
else
    echo "[BUILD] $NEOBUILD_OBJ is up to date."
fi
echo ""

# Collect all test source files
TEST_FILES=$(find "$TEST_DIR" -maxdepth 1 -name 'test_*.c' | sort)
TOTAL=0
PASSED=0
FAILED=0
FAILED_NAMES=""

# Disable set -e for the test execution phase
set +e

for TEST_SRC in $TEST_FILES; do
    TEST_NAME=$(basename "$TEST_SRC" .c)
    TEST_BIN="$BIN_DIR/$TEST_NAME"

    echo "----------------------------------------"
    echo "[COMPILE] $TEST_SRC"

    # Compilation must succeed
    $CC -o "$TEST_BIN" "$TEST_SRC" "$NEOBUILD_OBJ" \
        -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
        -I"$PROJECT_ROOT" -I"$PROJECT_ROOT/$TEST_DIR" -lm 2>&1
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to compile $TEST_SRC"
        FAILED=$((FAILED + 1))
        TOTAL=$((TOTAL + 1))
        FAILED_NAMES="$FAILED_NAMES  $TEST_NAME (compile error)\n"
        continue
    fi

    echo "[RUN]     $TEST_NAME"
    "$TEST_BIN"
    EXIT_CODE=$?
    TOTAL=$((TOTAL + 1))

    if [ $EXIT_CODE -eq 0 ]; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        FAILED_NAMES="$FAILED_NAMES  $TEST_NAME (exit $EXIT_CODE)\n"
    fi
done

echo ""
echo "========================================"
echo " Summary"
echo "========================================"
echo ""
echo "  Total suites: $TOTAL"
echo "  Passed:       $PASSED"
echo "  Failed:       $FAILED"

if [ -n "$FAILED_NAMES" ]; then
    echo ""
    echo "  Failed tests:"
    echo -e "$FAILED_NAMES"
fi

echo ""

# Clean up test binaries
rm -rf "$BIN_DIR"

if [ $FAILED -gt 0 ]; then
    echo "RESULT: FAIL"
    exit 1
else
    echo "RESULT: PASS"
    exit 0
fi
