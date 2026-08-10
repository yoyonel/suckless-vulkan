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
	export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
	export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
fi

echo "Exécution du smoke test (timeout 2s) : $APP_BIN"

set +e
if [ "$USE_XVFB" = true ]; then
	echo "Utilisation de xvfb-run..."
	xvfb-run -a -s "-screen 0 1920x1080x24" timeout -k 1s 2s "$APP_BIN" --no-vsync
else
	echo "Serveur X détecté, exécution directe..."
	timeout -k 1s 2s "$APP_BIN" --no-vsync
fi
EXIT_CODE=$?
set -e

if [ $EXIT_CODE -eq 124 ]; then
	echo "Smoke test passed (app ran for 2 seconds and was killed by timeout)."
	exit 0
elif [ $EXIT_CODE -eq 0 ]; then
	echo "Smoke test passed (app exited gracefully)."
	exit 0
else
	echo "Smoke test FAILED! App crashed or failed to load with exit code $EXIT_CODE."
	exit $EXIT_CODE
fi
