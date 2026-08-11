import csv
import sys

import numpy as np

frame_times_ms = []

try:
    with open("profiling/tracy_frames.csv", "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            ns = int(row["exec_time_ns"])
            frame_times_ms.append(ns / 1e6)
except FileNotFoundError:
    print("File not found.")
    sys.exit(1)

if not frame_times_ms:
    print("No frame data found.")
    sys.exit(0)

# Sort frame times descending (worst frame times = lowest FPS)
frame_times_ms = np.array(frame_times_ms)
fps = 1000.0 / frame_times_ms
fps = np.sort(fps)  # Lowest FPS first

print(f"Total Frames: {len(fps)}")
print(f"Average FPS: {fps.mean():.2f}")
print(f"Max FPS: {fps.max():.2f}")
print(f"Min FPS: {fps.min():.2f}")

percentiles = [1, 5, 10, 25, 50, 75, 90, 95, 99]
print("\n--- FPS Percentiles ---")
for p in percentiles:
    val = np.percentile(fps, p)
    print(f"{p}th Percentile (worst {p}%): {val:.2f} FPS")

# Also, find the consecutive frames that drop below 30 FPS to identify the transition period
low_fps_frames = []
for i, f in enumerate(1000.0 / frame_times_ms):
    if f < 30.0:
        low_fps_frames.append((i, f))

if low_fps_frames:
    print(f"\nFound {len(low_fps_frames)} frames below 30 FPS (Transition spikes)")
    print("Worst transition frames:")
    low_fps_frames.sort(key=lambda x: x[1])
    for i, f in low_fps_frames[:20]:
        print(f" Frame {i}: {f:.2f} FPS ({1000.0 / f:.2f} ms)")
