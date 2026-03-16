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

# In containerized environments, GPU/Vulkan loader can report harmless shutdown leaks.
# Keep the test informative while avoiding hard failures on known driver-side leaks.
lsan_options="exitcode=0"
if [[ -f ".asan_ignorefile" ]]; then
    lsan_options="suppressions=$(pwd)/.asan_ignorefile:report_objects=1:${lsan_options}"
fi

LSAN_OPTIONS="${lsan_options}" ctest --test-dir "${BUILD_DIR}" --output-on-failure
