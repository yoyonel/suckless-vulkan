#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

APP_BIN="${APP_BIN:-$REPO_ROOT/build/tracy/vulkan_app}"
CAPTURE_BIN="${CAPTURE_BIN:-$REPO_ROOT/build/tracy-capture/tracy-capture}"
WINDOW_NAME="${WINDOW_NAME:-Vulkan - Icosphere Full GPU}"
CAPTURE_SECONDS="${1:-12}"
TRACE_FILE="${2:-$REPO_ROOT/build/tracy/integration.tracy}"
LOG_DIR="${LOG_DIR:-$REPO_ROOT/build/tracy}"
APP_LOG="$LOG_DIR/tracy_integration_app.log"
CAPTURE_LOG="$LOG_DIR/tracy_integration_capture.log"
ENABLE_FULLSCREEN_TOGGLE="${ENABLE_FULLSCREEN_TOGGLE:-0}"

XVFB_PID=""
APP_PID=""
CAPTURE_PID=""

cleanup() {
	if [[ -n "$APP_PID" ]] && kill -0 "$APP_PID" 2>/dev/null; then
		kill -SIGTERM "$APP_PID" 2>/dev/null || true
		sleep 1
		if kill -0 "$APP_PID" 2>/dev/null; then
			kill -9 "$APP_PID" 2>/dev/null || true
		fi
	fi
	if [[ -n "$CAPTURE_PID" ]] && kill -0 "$CAPTURE_PID" 2>/dev/null; then
		kill -SIGTERM "$CAPTURE_PID" 2>/dev/null || true
		sleep 1
		if kill -0 "$CAPTURE_PID" 2>/dev/null; then
			kill -9 "$CAPTURE_PID" 2>/dev/null || true
		fi
	fi
	if [[ -n "$XVFB_PID" ]] && kill -0 "$XVFB_PID" 2>/dev/null; then
		kill -9 "$XVFB_PID" 2>/dev/null || true
	fi
}

require_cmd() {
	local name="$1"
	if ! command -v "$name" >/dev/null 2>&1; then
		echo "Error: required command not found: $name"
		exit 1
	fi
}

wait_for_window_start() {
	local pid="$1"
	local name="$2"
	local retries=40

	for ((i = 0; i < retries; i++)); do
		if ! kill -0 "$pid" 2>/dev/null; then
			echo "Error: app process died before window appeared."
			return 1
		fi

		local wid
		wid=$(xdotool search --sync --name "$name" 2>/dev/null | head -n 1 || true)
		if [[ -n "$wid" ]]; then
			echo "$wid"
			return 0
		fi

		sleep 0.25
	done

	echo "Error: window '$name' not found."
	return 1
}

focus_window() {
	# Do NOT steal focus from the user's active window.
	# xdotool key --window $wid sends keys directly to the target window
	# without requiring WM focus activation.
	local wid="$1"
	xdotool set_window --name "suckless-vulkan-test" "$wid" 2>/dev/null || true
}

run_scenario() {
	local wid="$1"
	echo "[scenario] switch HDR next"
	xdotool key --window "$wid" --delay 120 Page_Up
	sleep 1

	echo "[scenario] switch HDR previous"
	xdotool key --window "$wid" --delay 120 Page_Down
	sleep 2

	echo "[scenario] adjust env LOD"
	xdotool key --window "$wid" --delay 120 shift+Page_Up
	sleep 1
	xdotool key --window "$wid" --delay 120 shift+Page_Down
	sleep 1

	if [[ "$ENABLE_FULLSCREEN_TOGGLE" == "1" ]]; then
		echo "[scenario] fullscreen toggle"
		xdotool key --window "$wid" --delay 120 F11
		sleep 1
		xdotool key --window "$wid" --delay 120 F11
		sleep 1
	fi

	# Allow async HDR decode + upload + IBL bake to complete before shutdown.
	sleep 2

}

trap cleanup EXIT

require_cmd Xvfb
require_cmd xdotool

if [[ ! -x "$APP_BIN" ]]; then
	echo "Error: app binary not found or not executable: $APP_BIN"
	exit 1
fi

if [[ ! -x "$CAPTURE_BIN" ]]; then
	echo "Error: tracy capture binary not found or not executable: $CAPTURE_BIN"
	exit 1
fi

mkdir -p "$LOG_DIR"

# Use real GPU if a display is available (fast, ~2s IBL bake via iGPU).
# Fallback to Xvfb + llvmpipe in CI or when no display is present.
# NOTE: Intel Vulkan ICD requires DRI3 and cannot run under Xvfb.
if [[ "${CI:-}" == "1" ]] || [[ -z "${DISPLAY:-}" ]]; then
	DISPLAY_NUM=99
	while [[ -e "/tmp/.X${DISPLAY_NUM}-lock" ]]; do
		DISPLAY_NUM=$((DISPLAY_NUM + 1))
	done
	echo "[xvfb] no display found, starting Xvfb :$DISPLAY_NUM (CI/headless mode)"
	Xvfb ":${DISPLAY_NUM}" -screen 0 1920x1080x24 >/dev/null 2>&1 &
	XVFB_PID=$!
	sleep 1
	export DISPLAY=":${DISPLAY_NUM}"
	# llvmpipe: only SW renderer compatible with Xvfb (no DRI3 required)
	if [[ -z "${VK_ICD_FILENAMES:-}" ]] && [[ -f "/usr/share/vulkan/icd.d/lvp_icd.json" ]]; then
		export VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/lvp_icd.json"
		export VK_DRIVER_FILES="$VK_ICD_FILENAMES"
		echo "[gpu] Xvfb mode: using llvmpipe (slow IBL bake expected)"
	fi
else
	echo "[gpu] using existing DISPLAY ($DISPLAY) with real GPU (fast IBL bake)"
fi

echo "[capture] writing trace to $TRACE_FILE"
"$CAPTURE_BIN" -a 127.0.0.1 -p 8086 -o "$TRACE_FILE" -f -s "$CAPTURE_SECONDS" >"$CAPTURE_LOG" 2>&1 &
CAPTURE_PID=$!

echo "[app] starting $APP_BIN"
"$APP_BIN" --no-focus >"$APP_LOG" 2>&1 &
APP_PID=$!

WID=$(wait_for_window_start "$APP_PID" "$WINDOW_NAME")
focus_window "$WID"

echo "[scenario] waiting for app to finish IBL baking..."
timeout=60
elapsed=0
while ((elapsed < timeout)); do
	if grep -q "Initialization complete" "$APP_LOG" 2>/dev/null; then
		break
	fi
	sleep 1
	elapsed=$((elapsed + 1))
done

if ((elapsed >= timeout)); then
	echo "Error: app took too long to initialize (>$timeout s). See $APP_LOG"
	cleanup
	exit 1
fi
echo "[scenario] app initialized in ${elapsed}s, starting inputs."

run_scenario "$WID"

if ! wait "$CAPTURE_PID"; then
	echo "Error: tracy capture failed. See $CAPTURE_LOG"
	exit 1
fi
CAPTURE_PID=""

echo "[scenario] trace capture completed, terminating app..."
kill -SIGTERM "$APP_PID" 2>/dev/null || true

# Tracy client may hang flushing network data after capture disconnects.
# Wait up to 5s for clean exit, then force-kill.
APP_DEADLINE=5
app_wait=0
while kill -0 "$APP_PID" 2>/dev/null && ((app_wait < APP_DEADLINE)); do
	sleep 1
	app_wait=$((app_wait + 1))
done
if kill -0 "$APP_PID" 2>/dev/null; then
	echo "[warn] app did not exit after ${APP_DEADLINE}s, sending SIGKILL"
	kill -9 "$APP_PID" 2>/dev/null || true
fi

wait_status=0
wait "$APP_PID" || wait_status=$?
if [[ "$wait_status" != "0" && "$wait_status" != "143" && "$wait_status" != "130" && "$wait_status" != "137" ]]; then
	echo "Error: app exited with failure (code $wait_status). See $APP_LOG"
	exit 1
fi
APP_PID=""
CAPTURE_PID=""

if [[ ! -s "$TRACE_FILE" ]]; then
	echo "Error: trace file not generated or empty ($TRACE_FILE)"
	exit 1
fi

frames=$(grep -oP 'Frames: \K\d+' "$CAPTURE_LOG" || echo "0")
zones=$(grep -oP 'Zones: \K\d+' "$CAPTURE_LOG" || echo "0")

echo "================================================="
echo "Integration Test Trace Statistics:"
echo "Frames Captured: $frames"
echo "Zones Captured: $zones"
echo "================================================="

if ((frames < 10)); then
	echo "Error: trace captured too few frames ($frames). App may have failed to initialize properly or bake took too long."
	exit 1
fi

echo "SUCCESS: trace generated and validated at $TRACE_FILE"
echo "Capture log: $CAPTURE_LOG"
echo "App log: $APP_LOG"
