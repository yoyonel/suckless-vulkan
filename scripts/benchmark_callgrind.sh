#!/usr/bin/env bash
set -euo pipefail

echo "========================================="
echo "   BENCHMARK CALLGRIND (Valgrind)"
echo "========================================="

APP_BIN="./build/relwithdebinfo/vulkan_app"
if [ ! -f "$APP_BIN" ]; then
	echo "Erreur: $APP_BIN manquant. Lancez 'just build-relwithdebinfo'."
	exit 1
fi

if ! command -v valgrind >/dev/null 2>&1; then
	echo "Erreur: valgrind n'est pas installé sur le système."
	exit 1
fi

OUT_DIR="./build/profiling/callgrind"
mkdir -p "$OUT_DIR"

CALLGRIND_OUT="$OUT_DIR/callgrind.out"
SUMMARY_FILE="$OUT_DIR/callgrind_summary.txt"
rm -f "$CALLGRIND_OUT" "$SUMMARY_FILE"

TMP_DIR=$(mktemp -d)
export TMP_DIR
trap 'rm -rf "$TMP_DIR"' EXIT

echo "[callgrind] Exécution de l'application sous Valgrind Callgrind..."
env TMP_DIR="$TMP_DIR" ./scripts/interactive_runner.sh valgrind --tool=callgrind --dump-instr=yes --collect-jumps=yes --callgrind-out-file="$CALLGRIND_OUT" "$APP_BIN" --no-vsync

if [ -f "$CALLGRIND_OUT" ]; then
	if command -v callgrind_annotate >/dev/null 2>&1; then
		echo "[callgrind] Génération du rapport avec callgrind_annotate..."
		callgrind_annotate --auto=yes "$CALLGRIND_OUT" >"$SUMMARY_FILE" 2>&1 || true

		echo ""
		echo "=========================================================================="
		echo "📊 TOP FONCTIONS PAR INSTRUCTIONS CPU (Callgrind)"
		echo "=========================================================================="
		head -n 40 "$SUMMARY_FILE" || true
		FRAMES=$(grep -oE "Total frames rendered during this run: [0-9]+" "$TMP_DIR/runner_app.log" 2>/dev/null | awk '{print $NF}' | tail -n1 || echo "0")
		if [ -n "$FRAMES" ] && [ "$FRAMES" -gt 0 ] 2>/dev/null; then
			TOTAL_IR=$(grep -oE "[0-9,]+ \(100.0%\)  PROGRAM TOTALS" "$SUMMARY_FILE" 2>/dev/null | head -n1 | awk '{print $1}' | tr -d ',' || echo "0")
			if [ "$TOTAL_IR" -gt 0 ] 2>/dev/null; then
				IR_PER_FRAME=$(awk -v i="$TOTAL_IR" -v f="$FRAMES" 'BEGIN { printf "%.1f", i/f }')
				echo "🎯 Frames rendues : $FRAMES (~$IR_PER_FRAME instructions CPU / frame)"
			fi
		fi
		echo "=========================================================================="
	fi

	echo ""
	echo "✅ Fichier Callgrind généré : $CALLGRIND_OUT"
	echo "👉 Pour explorer l'arbre d'appel visuellement : kcachegrind $CALLGRIND_OUT"
else
	echo "Erreur: Fichier de sortie Callgrind introuvable."
	exit 1
fi
