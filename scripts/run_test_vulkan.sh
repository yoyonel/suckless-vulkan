#!/bin/bash
set -e

TEST_EXEC="$1"

if [ -z "$TEST_EXEC" ]; then
	echo "Usage: $0 <test_executable>"
	exit 1
fi

# Détection de l'environnement CI ou absence de serveur X
USE_XVFB=false
if [[ "$CI" == "true" ]] || [[ -z "$DISPLAY" ]]; then
	USE_XVFB=true
	# Force Lavapipe (CPU) en CI pour éviter les soucis de drivers GPU
	export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
	export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
fi

echo "Exécution du test : $TEST_EXEC"

if [ "$USE_XVFB" = true ]; then
	echo "Utilisation de xvfb-run..."
	xvfb-run -a -s "-screen 0 1920x1080x24" "$TEST_EXEC"
else
	echo "Serveur X détecté, exécution directe..."
	"$TEST_EXEC"
fi
