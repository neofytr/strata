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
SRCDIR="buildsysdep"
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

# Compile all neo_*.c files if needed
NEOBUILD_OBJS=""
NEO_SRCS="neo_core neo_arena neo_platform neo_command neo_deps neo_compile neo_graph neo_toolchain neo_detect neo_install neo_test_runner neo_config"

for src in $NEO_SRCS; do
    SRC_FILE="$SRCDIR/${src}.c"
    OBJ_FILE="$SRCDIR/${src}.o"
    if [ ! -f "$OBJ_FILE" ] || [ "$SRC_FILE" -nt "$OBJ_FILE" ] || [ "$SRCDIR/neo_internal.h" -nt "$OBJ_FILE" ] || [ "$SRCDIR/neobuild.h" -nt "$OBJ_FILE" ]; then
        echo "[BUILD] Compiling $SRC_FILE ..."
        $CC -c "$SRC_FILE" -o "$OBJ_FILE" -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
    fi
    NEOBUILD_OBJS="$NEOBUILD_OBJS $OBJ_FILE"
done
echo "[BUILD] Done."
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
    $CC -o "$TEST_BIN" "$TEST_SRC" $NEOBUILD_OBJS \
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
