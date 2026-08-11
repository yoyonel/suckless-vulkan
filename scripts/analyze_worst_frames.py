import csv


def analyze():
    # Read all zones
    zones = []
    with open("profiling/all_zones.csv", "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            zones.append(
                {
                    "name": row["name"],
                    "time_ns": int(row["ns_since_start"]),
                    "exec_time_ns": int(row["exec_time_ns"]),
                }
            )

    # Find vk_draw_frame_internal frames
    frames = [z for z in zones if z["name"] == "vk_draw_frame_internal"]
    frames.sort(key=lambda x: x["exec_time_ns"], reverse=True)

    # Take the top 2 worst frames
    worst_frames = frames[:2]

    for i, frame in enumerate(worst_frames):
        print(f"\n--- WORST FRAME #{i + 1} ---")
        print(f"Start Time: {frame['time_ns']} ns")
        print(f"Duration: {frame['exec_time_ns'] / 1e6:.2f} ms")

        start = frame["time_ns"]
        end = start + frame["exec_time_ns"]

        # Find all zones that fall completely or partially inside this frame
        inside_zones = []
        for z in zones:
            z_start = z["time_ns"]
            z_end = z_start + z["exec_time_ns"]
            if (
                z_start >= start
                and z_end <= end
                and z["name"] != "vk_draw_frame_internal"
            ):
                inside_zones.append(z)

        # Sort by exec time to see the culprits
        inside_zones.sort(key=lambda x: x["exec_time_ns"], reverse=True)

        print("Top 10 most expensive sub-zones:")
        for z in inside_zones[:10]:
            print(f"  - {z['name']}: {z['exec_time_ns'] / 1e6:.2f} ms")


if __name__ == "__main__":
    analyze()
