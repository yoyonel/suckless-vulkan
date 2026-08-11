#!/bin/bash
set -e

TRACE_FILE="build/tracy/benchmark.tracy"
if [ ! -f "$TRACE_FILE" ]; then
	echo "Erreur: Trace $TRACE_FILE introuvable. Lancez 'just benchmark-tracy' d'abord."
	exit 1
fi

CSVEXPORT_BIN="./build/tracy-csvexport/tracy-csvexport"
if [ ! -f "$CSVEXPORT_BIN" ]; then
	echo "Erreur: $CSVEXPORT_BIN manquant. Lancez 'just build-tracy-csvexport'."
	exit 1
fi

mkdir -p profiling
echo "Extraction des frames via vk_draw_frame_internal..."
"$CSVEXPORT_BIN" -u -f "vk_draw_frame_internal" "$TRACE_FILE" >profiling/tracy_frames.csv

echo "Analyse des FPS (Percentiles)..."
python3 scripts/analyze_fps.py
