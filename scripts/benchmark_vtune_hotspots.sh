#!/usr/bin/env bash
set -euo pipefail

echo "========================================="
echo "   BENCHMARK VTUNE (CPU Hotspots)"
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

RES_DIR="/tmp/vtune_results_hotspots_$(date +%s)"
OUT_DIR="./build/profiling/vtune"
mkdir -p "$OUT_DIR"

TMP_DIR=$(mktemp -d)
export TMP_DIR
trap 'rm -rf "$TMP_DIR"' EXIT

SUDO_CMD=""
if sudo -n true 2>/dev/null; then
	SUDO_CMD="sudo -E"
fi

echo "[vtune] Collection hotspots..."
$SUDO_CMD "$VTUNE_BIN" -collect hotspots -result-dir "$RES_DIR" env TMP_DIR="$TMP_DIR" ./scripts/interactive_runner.sh "$APP_BIN" --no-vsync
if [ -n "$SUDO_CMD" ]; then
	sudo chown -R "$USER":"$USER" "$RES_DIR"
fi

echo ""
echo "=========================================================================="
echo "📊 TOP 15 CPU HOTSPOTS (Fonctions les plus coûteuses en temps CPU)"
echo "=========================================================================="
SUMMARY_FILE="$OUT_DIR/vtune_hotspots_summary.txt"
"$VTUNE_BIN" -report hotspots -r "$RES_DIR" -format=text -limit=15 | grep -v "^vtune:" | c++filt >"$SUMMARY_FILE" 2>&1 || true
cat "$SUMMARY_FILE"
echo "=========================================================================="

echo ""
echo "✅ Résultats enregistrés dans : $RES_DIR"
echo "👉 Pour explorer visuellement le code et l'assembleur : vtune-gui $RES_DIR"
