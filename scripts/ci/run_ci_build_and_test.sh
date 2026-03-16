#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Release}"

if [[ "$BUILD_TYPE" != "Release" && "$BUILD_TYPE" != "Debug" ]]; then
    echo "BUILD_TYPE must be Release or Debug"
    exit 2
fi

echo "[CI] Build type: $BUILD_TYPE"

# Keep shader generation aligned with local/dev workflow.
just shaders

cmake -S . -B "build/${BUILD_TYPE}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "build/${BUILD_TYPE}" --parallel
ctest --test-dir "build/${BUILD_TYPE}" --output-on-failure
