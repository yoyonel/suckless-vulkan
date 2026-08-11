#!/bin/bash
set -e

APP_BIN="./build/release/vulkan_app"
if [ ! -f "$APP_BIN" ]; then
	echo "Erreur: $APP_BIN manquant. Lancez 'just build'."
	exit 1
fi

USE_XVFB=""
if [[ "$CI" == "true" ]] || [[ -z "$DISPLAY" ]]; then
	USE_XVFB="xvfb-run -a -s \"-screen 0 1920x1080x24\""
	export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
	export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
fi

TMP_DIR=$(mktemp -d)
export TMP_DIR

echo "========================================="
echo "   BENCHMARK CPU CACHE (perf stat)       "
echo "========================================="
set +e
eval "$USE_XVFB ./scripts/interactive_runner.sh perf stat -e L1-dcache-load-misses,L1-dcache-loads,L2-load-misses,LLC-load-misses,cpu-migrations $APP_BIN --no-vsync"
grep -E "(L1-dcache|L2-load|LLC-load|cpu-migrations|Performance counter stats)" -A 8 "$TMP_DIR/runner_app.log" || echo "Erreur: perf stat output introuvable"
set -e

echo ""
echo "========================================="
echo "   BENCHMARK HEAP ALLOCATIONS (heaptrack)"
echo "========================================="
set +e
# Heaptrack s'attache directement à vulkan_app. interactive_runner simulera xdotool puis kill.
eval "$USE_XVFB ./scripts/interactive_runner.sh heaptrack --record-only $APP_BIN --no-vsync" >"$TMP_DIR/heaptrack_log.txt" 2>&1 || true
set -e

# Extraction du chemin du fichier généré depuis la sortie console
HT_REAL_FILE=$(grep "heaptrack output will be written to" "$TMP_DIR/heaptrack_log.txt" | awk '{print $NF}' | tr -d '"' || true)
if [ -z "$HT_REAL_FILE" ] || [ ! -f "$HT_REAL_FILE" ]; then
	HT_REAL_FILE=$(find . -maxdepth 1 -type f \( -name 'heaptrack.*.zst' -o -name 'heaptrack.*.gz' \) -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | cut -d' ' -f2- || true)
fi

if [ -f "$HT_REAL_FILE" ]; then
	echo "Fichier de profiling trouvé : $HT_REAL_FILE"
	echo "Analyse en cours via heaptrack_print..."
	echo "--- RÉSULTATS MÉMOIRE ---"

	# On extrait la partie Summary du bas (Peak memory, leaks, allocs)
	heaptrack_print "$HT_REAL_FILE" >"$TMP_DIR/ht_print.txt"

	grep -E "peak heap memory consumption:" "$TMP_DIR/ht_print.txt"
	grep -E "calls to allocation functions:" "$TMP_DIR/ht_print.txt"
	grep -E "temporary allocations:" "$TMP_DIR/ht_print.txt" || true
	grep -E "total memory leaked:" "$TMP_DIR/ht_print.txt" || true

	echo "--- TOP 3 ENDROITS D'ALLOCATION ---"
	# Afficher les 3 lignes après "MOST CALLS TO ALLOCATION FUNCTIONS"
	awk '/MOST CALLS TO ALLOCATION FUNCTIONS/{flag=1; count=0; next} flag && count<6 {print; count++}' "$TMP_DIR/ht_print.txt" | grep -v "^$" | head -n 4 || true

	echo "Nettoyage du fichier trace..."
	rm -f "$HT_REAL_FILE"
else
	echo "Erreur : Fichier heaptrack non généré."
	cat "$TMP_DIR/heaptrack_log.txt"
fi

rm -rf "$TMP_DIR"
echo "========================================="
echo "   BENCHMARK TERMINÉ                     "
echo "========================================="
