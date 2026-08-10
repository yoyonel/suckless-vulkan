#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build/docker-asan"

echo "[CI][ASAN] Build directory: ${BUILD_DIR}"

# Keep shader generation aligned with local/dev workflow.
just shaders

cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_SANITIZERS=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "${BUILD_DIR}" --parallel

# En CI, on n'exécute QUE les LogicTests avec ASan pour éviter les faux positifs llvmpipe
lsan_options="suppressions=$(pwd)/.asan_ignorefile:report_objects=1"

LSAN_OPTIONS="${lsan_options}" ctest --test-dir "${BUILD_DIR}" --output-on-failure -R LogicTests
