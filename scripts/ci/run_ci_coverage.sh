#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build/coverage"
REPORT_DIR="${BUILD_DIR}/reports"

echo "[CI][COVERAGE] Build directory: ${BUILD_DIR}"

# Keep shader generation aligned with local/dev workflow.
just shaders

cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_COVERAGE=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "${BUILD_DIR}" --parallel
ctest --test-dir "${BUILD_DIR}" --output-on-failure -R LogicTests

mkdir -p "${REPORT_DIR}"

gcovr \
    --root . \
    --object-directory "${BUILD_DIR}" \
    --filter '^src/' \
    --exclude '^ext/' \
    --exclude '^tests/' \
    --txt-summary \
    --txt "${REPORT_DIR}/coverage.txt" \
    --xml "${REPORT_DIR}/coverage.xml" \
    --xml-pretty \
    --json "${REPORT_DIR}/coverage.json" \
    --html-details "${REPORT_DIR}/coverage.html"

echo "[CI][COVERAGE] Reports generated in ${REPORT_DIR}"

# Display formatted coverage report in console
if command -v python3 &> /dev/null; then
    echo ""
    python3 scripts/format_coverage_report.py "${REPORT_DIR}/coverage.json"
fi
