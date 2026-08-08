#!/bin/bash
set -euo pipefail

# Lancement de la commande (perf, heaptrack, ou l'app direct) en arrière-plan
"$@" >"$TMP_DIR/runner_app.log" 2>&1 &
APP_PID=$!

echo "[Runner] Application lancée. Attente 4s (Initialisation)..."
sleep 4

send_page_down() {
	# On cherche explicitement la fenêtre créée par vulkan_app
	WID=$(xdotool search --class "vulkan_app" 2>/dev/null | tail -1 || true)
	if [ -n "$WID" ]; then
		xdotool key --window "$WID" Page_Down
	else
		echo "[Runner] Attention: Fenêtre vulkan_app introuvable, annulation de la touche."
	fi
}

echo "[Runner] Changement HDR #1 (Touche Page_Down)..."
send_page_down
# L'IBL prend environ 1 à 2 secondes à se recalculer
sleep 5

echo "[Runner] Changement HDR #2 (Touche Page_Down)..."
send_page_down
sleep 5

echo "[Runner] Fin du test. Envoi de SIGINT à vulkan_app..."
killall -SIGINT vulkan_app || true
sleep 1
# Force kill au cas où
killall -SIGTERM vulkan_app 2>/dev/null || true
kill -SIGINT $APP_PID 2>/dev/null || true

echo "[Runner] Attente de la fin du processus pour flush..."
wait $APP_PID || true
echo "[Runner] Terminé."
