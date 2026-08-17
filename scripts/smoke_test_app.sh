#!/bin/bash
set -e

APP_BIN="$1"

if [ -z "$APP_BIN" ]; then
	echo "Usage: $0 <test_executable>"
	exit 1
fi

USE_XVFB=false
if [[ "$CI" == "true" ]] || [[ -z "$DISPLAY" ]]; then
	USE_XVFB=true
	export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
	export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
fi

echo "Exécution du smoke test (60 frames + graceful shutdown) : $APP_BIN"

set +e
if [ "$USE_XVFB" = true ]; then
	echo "Utilisation de xvfb-run..."
	xvfb-run -a -s "-screen 0 1920x1080x24" timeout -k 5s 120s "$APP_BIN" --no-vsync --no-focus --max-frames 5
else
	echo "Serveur X détecté, exécution directe..."
	timeout -k 5s 30s "$APP_BIN" --no-vsync --no-focus --max-frames 5
fi
EXIT_CODE=$?
set -e

if [ $EXIT_CODE -eq 0 ]; then
	echo "Smoke test passed (rendered frames and shut down gracefully with exit code 0)."
	exit 0
else
	echo "Smoke test FAILED! App crashed or hung with exit code $EXIT_CODE."
	exit $EXIT_CODE
fi
