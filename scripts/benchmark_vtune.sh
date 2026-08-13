#!/bin/bash
set -euo pipefail

echo "--- 🚀 Running VTune Memory Access Benchmark ---"

rm -rf ./vtune_results

# Source Intel vars if they exist
if [ -f /opt/intel/oneapi/setvars.sh ]; then
	# shellcheck disable=SC1091
	source /opt/intel/oneapi/setvars.sh --force >/dev/null 2>&1 || true
fi

# Run VTune with sudo (required for hardware events)
sudo -E /opt/intel/oneapi/vtune/2026.4/bin64/vtune -collect memory-access -result-dir ./vtune_results env TMP_DIR=/tmp ./scripts/interactive_runner.sh ./build/release/vulkan_app --no-vsync
