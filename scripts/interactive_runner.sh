#!/bin/bash
set -euo pipefail

"$@" >"$TMP_DIR/runner_app.log" 2>&1 &
APP_PID=$!

echo "[Runner] Application lancée. Attente 4s (Initialisation)..."
sleep 4

send_page_down() {
	WID=$(xdotool search --name "Vulkan" 2>/dev/null | tail -1 || true)
	if [ -n "$WID" ]; then
		echo "[Runner] Fenêtre trouvée (WID=$WID). Focus & Envoi de Page_Down..."
		xdotool windowfocus --sync "$WID" || true
		xdotool key Page_Down
	else
		echo "[Runner] Attention: Fenêtre introuvable, tentative d'envoi global..."
		xdotool key Page_Down
	fi

	echo "[Runner] Attente 5s (chargement IBL)..."
	sleep 5

	if grep -q "Chargement HDR async demande" "$TMP_DIR/runner_app.log"; then
		echo "[Runner] SUCCÈS: Changement HDR asynchrone confirmé par les logs."
	else
		echo "[Runner] Attention: Aucun log HDR. xdotool a pu échouer (WM manquant sous Xvfb?)."
	fi
}

echo "[Runner] Changement HDR #1 (Touche Page_Down)..."
send_page_down

echo "[Runner] Changement HDR #2 (Touche Page_Down)..."
send_page_down
killall -SIGINT vulkan_app || true
sleep 1
# Force kill au cas où
killall -SIGTERM vulkan_app 2>/dev/null || true
kill -SIGINT $APP_PID 2>/dev/null || true

echo "[Runner] Attente de la fin du processus pour flush..."
wait $APP_PID || true
echo "[Runner] Terminé."
