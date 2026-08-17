#!/usr/bin/env bash
set -euo pipefail

export PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:${PATH:-}"

echo "[CI][TRACY] Building with Tracy enabled..."
just shaders

# Clean CMake cache if directory path mismatch (e.g. host vs container mount)
if [[ -f build/tracy/CMakeCache.txt ]] && ! grep -q "$(pwd)" build/tracy/CMakeCache.txt 2>/dev/null; then
    echo "[CI][TRACY] Removing stale CMake build directories from host..."
    rm -rf build/tracy build/tracy-capture build/tracy-csvexport
fi

just build-tracy

scripts/smoke_test_app.sh build/tracy/vulkan_app
just test-integration-tracy
