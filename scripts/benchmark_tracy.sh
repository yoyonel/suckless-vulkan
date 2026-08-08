#!/bin/bash
set -e

APP_BIN="./build/tracy/vulkan_app"
if [ ! -f "$APP_BIN" ]; then
	echo "Erreur: $APP_BIN manquant. Lancez 'just build-tracy'."
	exit 1
fi

USE_XVFB=""
if [[ "$CI" == "true" ]] || [[ -z "$DISPLAY" ]]; then
	USE_XVFB="xvfb-run -a -s \"-screen 0 1920x1080x24\""
	export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
	export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
fi

TMP_DIR=$(mktemp -d)
export TMP_DIR

pkill -u "$USER" -f vulkan_app 2>/dev/null || true

echo "========================================="
echo "   BENCHMARK CPU CACHE (perf stat)       "
echo "========================================="
set +e
eval "$USE_XVFB ./scripts/interactive_runner.sh perf stat -e L1-dcache-load-misses,L1-dcache-loads $APP_BIN --no-vsync"
grep -E "(L1-dcache|Performance counter stats)" -A 5 "$TMP_DIR/runner_app.log" || echo "Erreur: perf stat output introuvable"
set -e

echo ""
echo "========================================="
echo "   BENCHMARK HEAP ALLOCATIONS (Tracy)    "
echo "========================================="

CAPTURE_BIN="./build/tracy-capture/tracy-capture"
if [ ! -f "$CAPTURE_BIN" ]; then
	echo "Erreur: $CAPTURE_BIN manquant. Lancez 'just build-tracy-capture'."
	exit 1
fi

CSVEXPORT_BIN="./build/tracy-csvexport/tracy-csvexport"
if [ ! -f "$CSVEXPORT_BIN" ]; then
	echo "Erreur: $CSVEXPORT_BIN manquant. Lancez 'just build-tracy-csvexport'."
	exit 1
fi

TRACE_FILE="build/tracy/benchmark.tracy"
CAPTURE_LOG="$TMP_DIR/capture.log"

echo "Lancement de l'application..."
set +e
eval "$USE_XVFB ./scripts/interactive_runner.sh $APP_BIN --no-vsync" &
RUNNER_PID=$!

echo "Attente de l'initialisation de l'application..."
sleep 3

echo "Lancement de tracy-capture en arriere-plan..."
"$CAPTURE_BIN" -o "$TRACE_FILE" -f -a 127.0.0.1 >"$CAPTURE_LOG" 2>&1 &
CAPTURE_PID=$!

echo "Attente de la fin de l'application..."
wait $RUNNER_PID || true
set -e

echo "Attente de la fin de la capture Tracy..."
wait $CAPTURE_PID || true

if [ -f "$TRACE_FILE" ]; then
	echo "Trace générée: $TRACE_FILE"
	echo "--- STATISTIQUES TRACY ---"
	grep -E "(Frames:|Zones:|Memory events:)" "$CAPTURE_LOG" || echo "Memory stats not explicitly in capture log."

	echo "Extraction des statistiques principales via csvexport..."
	"$CSVEXPORT_BIN" "$TRACE_FILE" >"$TMP_DIR/tracy_stats.csv" 2>/dev/null

	awk '
	BEGIN {
		FS = ","
		printf "\n%-35s | %-8s | %-10s | %-10s | %-10s\n", "ZONE TRACY", "% TEMPS", "APPELS", "MOY (ms)", "MAX (ms)"
		printf "------------------------------------+----------+------------+------------+------------\n"
	}
	NR > 1 && NF >= 9 {
		name = $1
		total_perc = $5
		counts = $6
		mean_ms = $7 / 1000000.0
		max_ms = $9 / 1000000.0
        
		if (length(name) > 34) {
			name = substr(name, 1, 31) "..."
		}
        
		printf "%-35s | %5.2f %% | %10d | %10.3f | %10.3f\n", name, total_perc, counts, mean_ms, max_ms
	}
	' "$TMP_DIR/tracy_stats.csv" | sort -t '|' -k2 -nr

	echo ""
	TOP_ZONE=$(tail -n +2 "$TMP_DIR/tracy_stats.csv" | sort -t ',' -k5 -nr | head -n 1 | awk -F',' '{print $1}')
	TOP_PERC=$(tail -n +2 "$TMP_DIR/tracy_stats.csv" | sort -t ',' -k5 -nr | head -n 1 | awk -F',' '{printf "%.1f", $5}')

	echo "🔍 Interprétation Rapide :"
	echo "- La zone la plus gourmande est '${TOP_ZONE}' consommant ~${TOP_PERC}% du temps processeur capturé."
	echo "- Utilisez Tracy Profiler GUI pour explorer l'historique complet, les flamegraphs et la timeline mémoire."
else
	echo "Erreur: Trace non générée."
	cat "$CAPTURE_LOG"
fi

rm -rf "$TMP_DIR"
echo "========================================="
echo "   BENCHMARK TERMINÉ                     "
echo "========================================="
