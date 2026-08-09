#!/bin/bash
set -e

APP_BIN="./build/release/vulkan_app"

if [ ! -f "$APP_BIN" ]; then
	echo "Executable $APP_BIN introuvable. Lancez un build release."
	exit 1
fi

USE_XVFB=false
if [[ "$CI" == "true" ]] || [[ -z "$DISPLAY" ]]; then
	USE_XVFB=true
	export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
	export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
fi

# Force une limite de mémoire virtuelle très basse (100MB)
ulimit -v 100000

echo "Exécution OOM Test (limite 100MB, timeout 3s) : $APP_BIN"

set +e
if [ "$USE_XVFB" = true ]; then
	xvfb-run -a -s "-screen 0 1920x1080x24" timeout -k 1s 3s "$APP_BIN" --no-vsync >/dev/null 2>&1
else
	timeout -k 1s 3s "$APP_BIN" --no-vsync >/dev/null 2>&1
fi
EXIT_CODE=$?
set -e

# Codes OOM/Segfault/Abort communs: 137 (SIGKILL), 139 (SIGSEGV), 134 (SIGABRT), ou tout > 128 (exception C++ bad_alloc non catchée)
if [ $EXIT_CODE -eq 137 ] || [ $EXIT_CODE -eq 139 ] || [ $EXIT_CODE -eq 134 ] || [ $EXIT_CODE -gt 128 ]; then
	echo "Succès : L'application a été tuée ou a planté proprement sous contrainte OOM (code $EXIT_CODE)."
	exit 0
elif [ $EXIT_CODE -eq 124 ]; then
	echo "Erreur : L'app n'a pas planté sous les 200MB. (Code 124: timeout). La limite est-elle trop haute ou ne consomme-t-on rien ?"
	exit 1
elif [ $EXIT_CODE -eq 0 ]; then
	echo "Erreur : L'app s'est fermée normalement (Code 0). Pas de plantage OOM."
	exit 1
else
	echo "Erreur inattendue : Code $EXIT_CODE."
	exit 1
fi
