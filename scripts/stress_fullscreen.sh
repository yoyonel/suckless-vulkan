#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

export LSAN_OPTIONS="${LSAN_OPTIONS:-suppressions=$REPO_ROOT/.asan_ignorefile}"

APP_BIN="${1:-$REPO_ROOT/build/release/vulkan_app}"
ITERATIONS="${2:-100}"
DELAY_MS="${3:-50}"
WINDOW_NAME="${WINDOW_NAME:-Vulkan - Icosphere Full GPU}"

APP_PID=""

cleanup() {
	if [[ -n "$APP_PID" ]] && kill -0 "$APP_PID" 2>/dev/null; then
		kill -SIGTERM "$APP_PID" 2>/dev/null || true
		sleep 0.5
		if kill -0 "$APP_PID" 2>/dev/null; then
			kill -9 "$APP_PID" 2>/dev/null || true
		fi
	fi
}
trap cleanup EXIT INT TERM

if ! command -v xdotool >/dev/null 2>&1; then
	echo "Error: xdotool is required"
	exit 1
fi

echo "Starting $APP_BIN..."
"$APP_BIN" &
APP_PID=$!

echo "Waiting for window '$WINDOW_NAME'..."
wid=""
for ((i = 0; i < 40; i++)); do
	if ! kill -0 "$APP_PID" 2>/dev/null; then
		echo "Error: App crashed during startup"
		exit 1
	fi
	wid=$(xdotool search --sync --name "$WINDOW_NAME" 2>/dev/null | head -n 1 || true)
	if [[ -n "$wid" ]]; then
		break
	fi
	sleep 0.25
done

if [[ -z "$wid" ]]; then
	echo "Error: Window not found"
	exit 1
fi

get_current_wid() {
	xdotool search --name "$WINDOW_NAME" 2>/dev/null | tail -n 1 || true
}

send_key() {
	local key="$1"
	local w
	w=$(get_current_wid)
	if [[ -n "$w" ]]; then
		xdotool key --window "$w" "$key" 2>/dev/null || true
	fi
}

echo "Starting stress test ($ITERATIONS iterations, $DELAY_MS ms delay)..."

for ((i = 1; i <= ITERATIONS; i++)); do
	if ! kill -0 "$APP_PID" 2>/dev/null; then
		echo "CRASH DETECTED on iteration $i!"
		exit 1
	fi

	send_key F11

	# Interleave periodic HDR map switches (every 5 iterations)
	if ((i % 5 == 0)); then
		send_key Page_Down
	elif ((i % 9 == 0)); then
		send_key Page_Up
	fi

	# Delay
	sleep "$(awk "BEGIN {print $DELAY_MS / 1000}")"
done

echo "Stress test loop completed! Shutting down gracefully..."
for ((k = 0; k < 5; k++)); do
	if ! kill -0 "$APP_PID" 2>/dev/null; then
		break
	fi
	send_key Escape
	sleep 0.2
done

if kill -0 "$APP_PID" 2>/dev/null; then
	kill -SIGTERM "$APP_PID" 2>/dev/null || true
fi

wait "$APP_PID" || {
	code=$?
	if [[ $code -ne 0 ]]; then
		echo "Error: App exited with code $code"
		exit 1
	fi
}
echo "App exited cleanly."
