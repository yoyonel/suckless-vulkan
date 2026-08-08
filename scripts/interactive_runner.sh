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
		xdotool windowfocus --sync "$WID" 2>/dev/null || true
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

# Tuer vulkan_app
APP_COMM=$(ps -p $APP_PID -o comm= 2>/dev/null || echo "")
if [ "$APP_COMM" = "perf" ]; then
	CHILD=$(pgrep -P "$APP_PID" || echo "")
	if [ -n "$CHILD" ]; then
		kill -SIGTERM "$CHILD" 2>/dev/null || true
		sleep 1
		kill -SIGKILL "$CHILD" 2>/dev/null || true
	fi
	# perf stat finira et écrira ses stats quand l'enfant mourra
else
	kill -SIGTERM "$APP_PID" 2>/dev/null || true
	sleep 1
	kill -SIGKILL "$APP_PID" 2>/dev/null || true
fi

echo "[Runner] Attente de la fin du processus pour flush..."
wait $APP_PID || true
echo "[Runner] Terminé."
