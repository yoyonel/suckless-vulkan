#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Release}"

case "$BUILD_TYPE" in
    build_type=Release)
        BUILD_TYPE="Release"
        ;;
    build_type=Debug)
        BUILD_TYPE="Debug"
        ;;
esac

if [[ "$BUILD_TYPE" != "Release" && "$BUILD_TYPE" != "Debug" ]]; then
    echo "BUILD_TYPE must be Release or Debug"
    exit 2
fi

echo "[CI] Sync test references with Docker rendering (${BUILD_TYPE})"

echo "[CI] Step 1/3: Regenerate references from the current renderer output"
SVK_UPDATE_REFERENCES=1 scripts/ci/run_ci_build_and_test.sh "$BUILD_TYPE"

echo "[CI] Step 2/3: Re-run tests in strict compare mode"
scripts/ci/run_ci_build_and_test.sh "$BUILD_TYPE"

echo "[CI] Step 3/3: Refresh reference checksum manifest"
refs_manifest="tests/references/manifest.sha256"
mkdir -p "tests/references"
find tests/references -maxdepth 1 -type f -name '*.png' -print0 \
    | sort -z \
    | xargs -0 sha256sum > "$refs_manifest"

echo "[CI] Reference manifest written: $refs_manifest"
