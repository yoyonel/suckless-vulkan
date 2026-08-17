#!/usr/bin/env bash
set -euo pipefail

echo "========================================="
echo "   BENCHMARK VTUNE (Memory Access & Cache Misses)"
echo "========================================="

APP_BIN="./build/relwithdebinfo/vulkan_app"
if [ ! -f "$APP_BIN" ]; then
	echo "Erreur: $APP_BIN manquant. Lancez 'just build-relwithdebinfo'."
	exit 1
fi

VTUNE_BIN=""
if command -v vtune >/dev/null 2>&1; then
	VTUNE_BIN=$(command -v vtune)
elif [ -x /opt/intel/oneapi/vtune/latest/bin64/vtune ]; then
	VTUNE_BIN="/opt/intel/oneapi/vtune/latest/bin64/vtune"
elif [ -d /opt/intel/oneapi/vtune ]; then
	VTUNE_BIN=$(find /opt/intel/oneapi/vtune -name vtune -type f -perm -111 2>/dev/null | grep bin64 | head -n1 || true)
fi

if [ -z "$VTUNE_BIN" ] || [ ! -x "$VTUNE_BIN" ]; then
	echo "Erreur: Intel VTune Profiler (vtune) introuvable dans PATH ou /opt/intel/oneapi/vtune."
	exit 1
fi

if [ -f /opt/intel/oneapi/setvars.sh ]; then
	# shellcheck disable=SC1091
	source /opt/intel/oneapi/setvars.sh --force >/dev/null 2>&1 || true
fi

RES_DIR="/tmp/vtune_results_memory_$(date +%s)"
OUT_DIR="./build/profiling/vtune"
mkdir -p "$OUT_DIR"

TMP_DIR=$(mktemp -d)
export TMP_DIR
trap 'rm -rf "$TMP_DIR"' EXIT

SUDO_CMD=""
COLLECT_TYPE="memory-access"
if sudo -n true 2>/dev/null; then
	SUDO_CMD="sudo -E"
else
	COLLECT_TYPE="performance-snapshot"
fi

echo "[vtune] Collection $COLLECT_TYPE..."
$SUDO_CMD "$VTUNE_BIN" -collect "$COLLECT_TYPE" -result-dir "$RES_DIR" env TMP_DIR="$TMP_DIR" ./scripts/interactive_runner.sh "$APP_BIN" --no-vsync
if [ -n "$SUDO_CMD" ]; then
	sudo chown -R "$USER":"$USER" "$RES_DIR"
fi

echo "[vtune] Rendu du rapport Memory Access..."
SUMMARY_FILE="$OUT_DIR/vtune_memory_summary.txt"
"$VTUNE_BIN" -report summary -r "$RES_DIR" -format=text >"$SUMMARY_FILE" 2>&1 || true

echo ""
echo "=========================================================================="
echo "📊 RÉSUMÉ MÉMOIRE & CACHE MISSES (Intel VTune)"
echo "=========================================================================="
grep -E "Memory Bound|L1 Bound|L2 Bound|L3 Bound|DRAM Bound|Load Handled|Store Handled|LLC Miss|Average Latency" "$SUMMARY_FILE" || head -n 35 "$SUMMARY_FILE" || true
FRAMES=$(grep -oE "Total frames rendered during this run: [0-9]+" "$TMP_DIR/runner_app.log" 2>/dev/null | awk '{print $NF}' | tail -n1 || echo "0")
if [ -n "$FRAMES" ] && [ "$FRAMES" -gt 0 ] 2>/dev/null; then
	LLC_MISSES=$(grep -oE "LLC Miss Count: [0-9,]+" "$SUMMARY_FILE" 2>/dev/null | awk '{print $NF}' | tr -d ',' || echo "0")
	if [ "$LLC_MISSES" -gt 0 ] 2>/dev/null; then
		MISSES_PER_FRAME=$(awk -v m="$LLC_MISSES" -v f="$FRAMES" 'BEGIN { printf "%.1f", m/f }')
		echo "🎯 Frames rendues : $FRAMES (~$MISSES_PER_FRAME LLC misses / frame)"
	fi
fi
echo "=========================================================================="

echo ""
echo "✅ Résultats enregistrés dans : $RES_DIR"
echo "👉 Pour explorer visuellement : vtune-gui $RES_DIR"
