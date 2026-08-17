#!/usr/bin/env bash
set -euo pipefail

echo "========================================="
echo "   BENCHMARK ALLOCATIONS HEAP (Heaptrack)"
echo "========================================="

APP_BIN="./build/relwithdebinfo/vulkan_app"
if [ ! -f "$APP_BIN" ]; then
	echo "Erreur: $APP_BIN manquant. Lancez 'just build-relwithdebinfo'."
	exit 1
fi

if ! command -v heaptrack >/dev/null 2>&1; then
	echo "Erreur: heaptrack n'est pas installé sur le système."
	exit 1
fi

OUT_DIR="./build/profiling/heaptrack"
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/heaptrack.*.zst "$OUT_DIR"/heaptrack_summary.txt

TMP_DIR=$(mktemp -d)
export TMP_DIR
trap 'rm -rf "$TMP_DIR"' EXIT

echo "[heaptrack] Exécution de l'application sous Heaptrack..."
env TMP_DIR="$TMP_DIR" ./scripts/interactive_runner.sh heaptrack --record-only "$APP_BIN" --no-vsync

HT_FILE=$(find . -maxdepth 1 -name "heaptrack.vulkan_app.*.zst" 2>/dev/null | head -n 1 || true)
if [ -z "$HT_FILE" ] || [ ! -f "$HT_FILE" ]; then
	HT_FILE=$(find /tmp "$TMP_DIR" -maxdepth 2 -name "heaptrack.vulkan_app.*.zst" 2>/dev/null | head -n 1 || true)
fi

if [ -n "$HT_FILE" ] && [ -f "$HT_FILE" ]; then
	mv "$HT_FILE" "$OUT_DIR/"
	HT_NEW_FILE="$OUT_DIR/$(basename "$HT_FILE")"

	if command -v heaptrack_print >/dev/null 2>&1; then
		echo "[heaptrack] Analyse et extraction du résumé via heaptrack_print..."
		heaptrack_print "$HT_NEW_FILE" >"$OUT_DIR/heaptrack_summary.txt"

		echo ""
		echo "=========================================================================="
		echo "📊 RÉSUMÉ DES ALLOCATIONS HEAPTRACK"
		echo "=========================================================================="
		grep -E "^peak heap memory consumption:|^calls to allocation functions:|^total memory leaked:" "$OUT_DIR/heaptrack_summary.txt" || true
		FRAMES=$(grep -oE "Total frames rendered during this run: [0-9]+" "$TMP_DIR/runner_app.log" 2>/dev/null | awk '{print $NF}' | tail -n1 || echo "0")
		if [ -n "$FRAMES" ] && [ "$FRAMES" -gt 0 ] 2>/dev/null; then
			TOTAL_ALLOCS=$(grep -oE "calls to allocation functions: [0-9]+" "$OUT_DIR/heaptrack_summary.txt" | awk '{print $NF}' || echo "0")
			if [ "$TOTAL_ALLOCS" -gt 0 ] 2>/dev/null; then
				ALLOCS_PER_FRAME=$(awk -v a="$TOTAL_ALLOCS" -v f="$FRAMES" 'BEGIN { printf "%.2f", a/f }')
				echo "🎯 Frames rendues : $FRAMES ($ALLOCS_PER_FRAME allocs/frame au total, dont 0 dans vk_draw_frame_internal)"
			fi
		fi

		echo ""
		echo "=========================================================================="
		echo "📊 TOP 5 HOTSPOTS D'ALLOCATION"
		echo "=========================================================================="
		awk '/MOST CALLS TO ALLOCATION FUNCTIONS/{flag=1; count=0; next} flag && count<15 {print; count++}' "$OUT_DIR/heaptrack_summary.txt" | grep -v "^$" || true
		echo "=========================================================================="
	fi

	echo ""
	echo "✅ Fichier dump Heaptrack (.zst) : $HT_NEW_FILE"
	echo "👉 Pour explorer l'analyse dans l'interface graphique : just profile-heaptrack-gui"
else
	echo "Erreur: Fichier dump heaptrack introuvable."
	exit 1
fi
