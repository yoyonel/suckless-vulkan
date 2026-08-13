#!/bin/bash
set -euo pipefail

echo "--- 🚀 Running Heaptrack Allocation Benchmark ---"

OUT_DIR="./heaptrack_results"
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/heaptrack.*.zst "$OUT_DIR"/heaptrack_summary.txt

# Fix Heaptrack breaking dlopen for RHI modules
export LD_LIBRARY_PATH="$PWD/build/release:${LD_LIBRARY_PATH:-}"

TMP_DIR=$(mktemp -d)
export TMP_DIR

# Nettoyage automatique du TMP_DIR à la sortie
trap 'rm -rf "$TMP_DIR"' EXIT

env TMP_DIR="$TMP_DIR" ./scripts/interactive_runner.sh heaptrack --record-only ./build/release/vulkan_app --no-vsync

HT_FILE=$(find . -maxdepth 1 -name "heaptrack.vulkan_app.*.zst" | head -n 1)
if [ -n "$HT_FILE" ] && [ -f "$HT_FILE" ]; then
	echo "Analysis via heaptrack_print..."
	mv "$HT_FILE" "$OUT_DIR/"
	HT_NEW_FILE="$OUT_DIR/$(basename "$HT_FILE")"

	heaptrack_print "$HT_NEW_FILE" >"$OUT_DIR/heaptrack_summary.txt"

	# Affichage du résumé
	echo -e "\n=== RÉSUMÉ DES ALLOCATIONS ==="
	grep -E "^peak heap memory consumption:|^calls to allocation functions:|^total memory leaked:" "$OUT_DIR/heaptrack_summary.txt" || true

	echo -e "\n=== TOP 5 HOTSPOTS D'ALLOCATION ==="
	awk '/MOST CALLS TO ALLOCATION FUNCTIONS/{flag=1; count=0; next} flag && count<12 {print; count++}' "$OUT_DIR/heaptrack_summary.txt" | grep -v "^$" || true
	echo -e "===================================\n"

	echo "✅ Les résultats et le fichier dump (.zst) ont été sauvegardés dans le dossier $OUT_DIR/"
	echo "👉 Pour examiner visuellement : heaptrack_gui $HT_NEW_FILE"
else
	echo "Erreur: Fichier dump heaptrack introuvable."
	exit 1
fi
