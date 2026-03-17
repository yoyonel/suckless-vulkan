#!/usr/bin/env bash
# Generate LLVM coverage report (llvm-cov) for better formatted summaries
set -euo pipefail

# Check for required tools
for tool in clang clang++ llvm-profdata llvm-cov; do
    if ! command -v "$tool" &> /dev/null; then
        echo "[ERROR] Required tool not found: $tool"
        echo "Install LLVM tools: sudo apt-get install llvm clang"
        exit 1
    fi
done

BUILD_DIR="build/coverage-llvm"
REPORT_DIR="${BUILD_DIR}/coverage_report"
PROFILE_DATA="${BUILD_DIR}/coverage.profdata"

echo "[LLVM-COV] Build directory: ${BUILD_DIR}"

# Keep shader generation aligned with local/dev workflow.
just shaders

# Configure with LLVM coverage instrumentation
echo "[LLVM-COV] Configuring with ENABLE_LLVM_COV=ON..."
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_LLVM_COV=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo "[LLVM-COV] Building with coverage instrumentation..."
cmake --build "${BUILD_DIR}" --parallel

# Run all tests (LogicTests + EngineIntegrationTest) with LLVM_PROFILE_FILE to generate .profraw files
echo "[LLVM-COV] Running ALL tests to generate profile data..."
mkdir -p "${BUILD_DIR}"
LLVM_PROFILE_FILE="${BUILD_DIR}/test_%p.profraw" ctest --test-dir "${BUILD_DIR}" --output-on-failure

# Merge profile data
echo "[LLVM-COV] Merging profile data..."
llvm-profdata merge -sparse "${BUILD_DIR}"/*.profraw -o "${PROFILE_DATA}"

# Generate HTML reports
echo "[LLVM-COV] Generating HTML report..."
mkdir -p "${REPORT_DIR}"

# Find all test executables
LOGIC_TESTS_BIN="${BUILD_DIR}/logic_tests"
UNIT_TESTS_BIN="${BUILD_DIR}/unit_tests"

if [ ! -f "${LOGIC_TESTS_BIN}" ] || [ ! -f "${UNIT_TESTS_BIN}" ]; then
    echo "Error: Test executables not found"
    [ ! -f "${LOGIC_TESTS_BIN}" ] && echo "  - logic_tests: ${LOGIC_TESTS_BIN}"
    [ ! -f "${UNIT_TESTS_BIN}" ] && echo "  - unit_tests: ${UNIT_TESTS_BIN}"
    exit 1
fi

llvm-cov show -format=html \
    -instr-profile="${PROFILE_DATA}" \
    "${UNIT_TESTS_BIN}" \
    -output-dir="${REPORT_DIR}" \
    -ignore-filename-regex='(tests/|ext/)' \
    > /dev/null 2>&1

# Print summary report to console for ALL source code
echo ""
echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
echo "📊 LLVM CODE COVERAGE SUMMARY REPORT (All Tests: LogicTests + EngineIntegrationTest)"
echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
echo ""

# Generate report using profdata (not individual binaries) to get all coverage data
llvm-cov report \
    -instr-profile="${PROFILE_DATA}" \
    "${UNIT_TESTS_BIN}" \
    "${LOGIC_TESTS_BIN}" \
    -ignore-filename-regex='(tests/|ext/)' 2>/dev/null || true

echo ""
echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
echo "🔗 HTML Report: ${REPORT_DIR}/index.html"
echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
echo ""

