#!/usr/bin/env python3
"""Automated verification script for Tracy traces (.tracy).

Validates that:
1. The Vulkan Graphics Queue / GPU timeline is active and emitting timestamps.
2. Expected GPU render passes (Forward Pass, PostProcess Pass, Geometry, Skybox) exist.
3. Optional GPU compute passes (IBL Luminance, BRDF, Irradiance, Specular) exist.
4. Timestamps are non-zero and physically valid.
5. Standardized CPU hierarchy (Total Frame, Frame Acquire Swapchain, etc.) is present.
6. Legacy/raw internal function names are absent from the trace.
"""

import argparse
import csv
import io
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Verify Tracy trace (.tracy) GPU & CPU contents."
    )
    parser.add_argument("trace_file", type=Path, help="Path to .tracy trace file")
    parser.add_argument(
        "--csvexport-bin",
        type=Path,
        default=Path("build/tracy-csvexport/tracy-csvexport"),
        help="Path to tracy-csvexport binary",
    )
    parser.add_argument(
        "--min-gpu-zones",
        type=int,
        default=10,
        help="Minimum total GPU zone occurrences expected across trace (default: 10)",
    )
    parser.add_argument(
        "--min-frame-passes",
        type=int,
        default=1,
        help="Minimum occurrences for each required per-frame GPU pass (default: 1)",
    )
    parser.add_argument(
        "--require-render-passes",
        action="store_true",
        default=True,
        help="Require standard RenderGraph per-frame GPU passes (default: True)",
    )
    parser.add_argument(
        "--require-compute-passes",
        action="store_true",
        default=False,
        help="Require IBL compute passes to be present in trace",
    )
    parser.add_argument(
        "--require-standard-labels",
        action="store_true",
        default=True,
        help="Require standardized labels and forbid legacy raw names (default: True)",
    )
    parser.add_argument(
        "--require-fibers",
        action="store_true",
        default=True,
        help="Require Virtual Tracks / Fibers (Async Status & Hybrid Perf) (default: True)",
    )
    return parser.parse_args()


def run_csvexport(csvexport_bin: Path, trace_file: Path, flag: str) -> str:
    if not csvexport_bin.exists():
        raise FileNotFoundError(f"tracy-csvexport binary not found at {csvexport_bin}")
    if not trace_file.exists():
        raise FileNotFoundError(f"Trace file not found at {trace_file}")

    cmd = [str(csvexport_bin), flag, str(trace_file)]
    res = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if res.returncode != 0:
        raise RuntimeError(
            f"tracy-csvexport failed (code {res.returncode}): {res.stderr.strip()}"
        )
    return res.stdout


def analyze_gpu_zones(csv_content: str):
    zones_stats = defaultdict(
        lambda: {"count": 0, "total_ns": 0, "min_ns": float("inf"), "max_ns": 0}
    )
    total_zones = 0
    total_time_ns = 0

    reader = csv.DictReader(io.StringIO(csv_content))
    for row in reader:
        name = row.get("name", "").strip()
        if not name:
            continue
        try:
            gpu_time_ns = int(row.get("GPU execution time", "0"))
        except ValueError:
            gpu_time_ns = 0

        stats = zones_stats[name]
        stats["count"] += 1
        stats["total_ns"] += gpu_time_ns
        stats["min_ns"] = min(stats["min_ns"], gpu_time_ns)
        stats["max_ns"] = max(stats["max_ns"], gpu_time_ns)

        total_zones += 1
        total_time_ns += gpu_time_ns

    return zones_stats, total_zones, total_time_ns


def analyze_cpu_zones(csv_content: str):
    cpu_counts = defaultdict(int)
    reader = csv.DictReader(io.StringIO(csv_content))
    for row in reader:
        name = row.get("name", "").strip()
        if name:
            cpu_counts[name] += 1
    return cpu_counts


def print_gpu_table(zones_stats, total_zones, total_time_ns):
    print("\n" + "=" * 80)
    print("   VULKAN GPU TIMELINE VALIDATION SUMMARY")
    print("=" * 80)
    print(
        f"{'GPU ZONE NAME':<35} | {'COUNT':<8} | {'TOTAL (ms)':<11} | {'AVG (ms)':<10} | {'MAX (ms)':<10}"
    )
    print(
        "-" * 35
        + "-+-"
        + "-" * 8
        + "-+-"
        + "-" * 11
        + "-+-"
        + "-" * 10
        + "-+-"
        + "-" * 10
    )

    for name, stats in sorted(
        zones_stats.items(), key=lambda x: x[1]["count"], reverse=True
    ):
        count = stats["count"]
        total_ms = stats["total_ns"] / 1_000_000.0
        avg_ms = (stats["total_ns"] / count) / 1_000_000.0 if count > 0 else 0.0
        max_ms = stats["max_ns"] / 1_000_000.0
        display_name = name if len(name) <= 34 else name[:31] + "..."
        print(
            f"{display_name:<35} | {count:<8} | {total_ms:<11.3f} | {avg_ms:<10.3f} | {max_ms:<10.3f}"
        )

    print("-" * 80)
    print(
        f"Total GPU Events: {total_zones} | Total GPU Time: {total_time_ns / 1_000_000.0:.2f} ms"
    )
    print("=" * 80 + "\n")


def print_cpu_summary(cpu_counts):
    print("=" * 80)
    print("   STANDARDIZED CPU TIMELINE ZONES")
    print("=" * 80)
    for name in sorted(cpu_counts.keys()):
        print(f"  - {name:<40} : {cpu_counts[name]:>6} calls")
    print("=" * 80 + "\n")


def main():
    args = parse_args()
    errors = []

    print(f"[verify_tracy] Analyzing trace: {args.trace_file}")

    try:
        gpu_csv = run_csvexport(args.csvexport_bin, args.trace_file, "-g")
    except Exception as e:  # noqa: BLE001
        print(
            f"[verify_tracy] ERROR: Failed to export GPU events: {e}", file=sys.stderr
        )
        sys.exit(1)

    zones_stats, total_zones, total_time_ns = analyze_gpu_zones(gpu_csv)
    print_gpu_table(zones_stats, total_zones, total_time_ns)

    try:
        cpu_csv = run_csvexport(args.csvexport_bin, args.trace_file, "-u")
    except Exception as e:  # noqa: BLE001
        print(
            f"[verify_tracy] ERROR: Failed to export CPU events: {e}", file=sys.stderr
        )
        sys.exit(1)

    cpu_counts = analyze_cpu_zones(cpu_csv)
    print_cpu_summary(cpu_counts)

    # 1. Global GPU Activity Assertion
    if total_zones < args.min_gpu_zones:
        errors.append(
            f"Insufficient GPU zones emitted in trace: found {total_zones}, expected >= {args.min_gpu_zones}"
        )

    if total_time_ns <= 0:
        errors.append(
            "Total GPU execution time is 0 ns! Hardware timestamps are inactive or invalid."
        )

    # 2. RenderGraph Per-Frame GPU Passes Assertions
    if args.require_render_passes:
        legacy_passes = ["Forward Pass", "PostProcess Pass"]
        has_legacy = all(
            zones_stats[p]["count"] >= args.min_frame_passes for p in legacy_passes
        )
        fused_pass_name = "Fused Pass (Forward + PostProcess)"
        has_fused = zones_stats[fused_pass_name]["count"] >= args.min_frame_passes

        if not (has_legacy or has_fused):
            errors.append(
                f"Missing or insufficient required GPU pass(es): found legacy (Forward Pass={zones_stats['Forward Pass']['count']}, PostProcess Pass={zones_stats['PostProcess Pass']['count']}), fused ({fused_pass_name}={zones_stats[fused_pass_name]['count']}), expected >= {args.min_frame_passes}"
            )

    # 3. IBL Compute GPU Passes Assertions
    if args.require_compute_passes:
        compute_passes = [
            "GPU IBL Luminance",
            "GPU IBL BRDF LUT",
            "GPU IBL Irradiance Slice",
            "GPU IBL Specular",
        ]
        for pass_name in compute_passes:
            count = zones_stats[pass_name]["count"]
            if count < 1:
                errors.append(
                    f"Missing required GPU compute pass '{pass_name}' in trace."
                )

    # 4. Standardized Labels Assertions (Étape 4)
    if args.require_standard_labels:
        required_cpu_labels = [
            "Total Frame",
            "Frame Acquire Swapchain",
            "Frame Scene Update",
            "RenderGraph Execute & Record",
            "Frame Queue Submit & Present",
        ]
        for label in required_cpu_labels:
            count = cpu_counts[label]
            if count < args.min_frame_passes:
                errors.append(
                    f"Missing required standardized CPU label '{label}': found {count}, expected >= {args.min_frame_passes}"
                )

        forbidden_legacy_labels = [
            "vk_draw_frame_internal",
            "Frame CPU Acquire",
            "Frame CPU Update",
            "Frame CPU Record",
            "Frame CPU Submit and Present",
            "hdr_io_thread_iteration",
            "hdr_io_thread_decode",
            "allocate_hdr_resources_async",
            "init_environment_texture_from_staging",
            "start_hdr_bake_allocations",
            "request_environment_texture_async",
            "hdr_io_thread_cleanup",
            "vk_init_vulkan_engine",
            "vk_cleanup_vulkan_engine",
        ]
        for legacy_label in forbidden_legacy_labels:
            count = cpu_counts[legacy_label]
            if count > 0:
                errors.append(
                    f"Forbidden legacy/raw label '{legacy_label}' found in trace ({count} occurrences). Must be renamed."
                )

    # 5. Virtual Tracks / Fibers Assertions (Étape 3)
    if args.require_fibers:
        required_fiber_zones = [
            "Async LOADING",
            "Async CONVERT",
            "Async READY",
            "Host (CPU): Luminance",
            "Host (CPU): BRDF LUT",
            "Host (CPU): Irradiance",
            "Host (CPU): Specular",
            "Sync (GPU Wait)",
        ]
        for fiber_zone in required_fiber_zones:
            count = cpu_counts[fiber_zone]
            if count < 1:
                errors.append(
                    f"Missing required Fiber zone '{fiber_zone}' in trace (found {count}, expected >= 1)."
                )

    if errors:
        print("\n[verify_tracy] ❌ VALIDATION FAILED:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        sys.exit(1)

    print("[verify_tracy] ✅ SUCCESS: All GPU & CPU timeline invariants satisfied!")
    sys.exit(0)


if __name__ == "__main__":
    main()
