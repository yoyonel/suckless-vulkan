#!/usr/bin/env python3
import re
import subprocess
import sys
from datetime import datetime, timezone

DOC_FILE = "docs/benchmarking_baseline.md"
TARGET = "./build/release/unit_tests"


def parse_baseline():
    try:
        with open(DOC_FILE, "r") as f:
            content = f.read()
    except FileNotFoundError:
        print(f"Error: {DOC_FILE} not found.", file=sys.stderr)
        sys.exit(1)

    # Find the Iteration 8 section (the official E2E baseline)
    iterations = re.findall(r"## Itération (\d+) : .*?\((.*?)\)", content)
    if not iterations:
        print("Error: Could not find any iterations in baseline doc.", file=sys.stderr)
        sys.exit(1)

    # Hardcode iter 8 which has the correct format
    last_iter_num, last_iter_date = next(
        (i for i in iterations if i[0] == "8"), iterations[-1]
    )

    # Extract the numbers from the last iteration
    last_iter_content = content[content.rfind(f"## Itération {last_iter_num}") :]

    # - **L1-dcache-load-misses (P-Core)** : **0.18%** (4.3 Millions misses / 2.37 Milliards loads).
    l1_match = re.search(
        r"L1-dcache-load-misses \(P-Core\).*?\(([\d.]+) Millions misses / ([\d.]+) Milliards loads\)",
        last_iter_content,
    )
    # - **LLC-loads (Requêtes L2 -> L3)** : **1.3 Millions**.
    llc_load_match = re.search(
        r"LLC-loads \(Requêtes L2 -> L3\).*?\*\*([\d.]+) Millions\*\*",
        last_iter_content,
    )
    # - **LLC-load-misses (Requêtes L3 -> RAM)** : **~556,521**.
    llc_miss_match = re.search(
        r"LLC-load-misses \(Requêtes L3 -> RAM\).*?\*\*~?([\d,]+)[kM]?\*\*",
        last_iter_content,
    )

    if not (l1_match and llc_load_match and llc_miss_match):
        print(
            "Error: Could not parse metrics from the last iteration.", file=sys.stderr
        )
        sys.exit(1)

    l1_misses_m = float(l1_match.group(1))
    l1_loads_b = float(l1_match.group(2))
    llc_loads_m = float(llc_load_match.group(1))

    # parse llc misses (might have commas, k or M)
    miss_str = llc_miss_match.group(1).replace(",", "")
    llc_misses = float(miss_str)

    return {
        "iter": int(last_iter_num),
        "date": last_iter_date,
        "l1_misses_m": l1_misses_m,
        "l1_loads_b": l1_loads_b,
        "llc_loads_m": llc_loads_m,
        "llc_misses": llc_misses,
    }


def run_benchmark():
    import os
    import tempfile

    tmp_dir = tempfile.mkdtemp()
    env = os.environ.copy()
    env["TMP_DIR"] = tmp_dir

    app_bin = "./build/release/vulkan_app"
    runner = "./scripts/interactive_runner.sh"

    cmd = [
        runner,
        "perf",
        "stat",
        "-x,",
        "-e",
        "L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads,instructions",
        app_bin,
        "--no-vsync",
    ]

    print("--- 🚀 Running Perf Benchmark (E2E) ---", file=sys.stderr)
    subprocess.run(
        cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        env=env,
        text=True,
        check=False,
    )

    log_file = os.path.join(tmp_dir, "runner_app.log")

    stats = {}
    if os.path.exists(log_file):
        with open(log_file, "r") as f:
            for line in f:
                if "Total frames rendered during this run:" in line:
                    m = re.search(
                        r"Total frames rendered during this run:\s*(\d+)", line
                    )
                    if m:
                        stats["frames"] = int(m.group(1))

                parts = line.split(",")
                if len(parts) >= 3:
                    val_str = parts[0]
                    event = parts[2]
                    if "cpu_core/" in event:
                        clean_event = event.replace("cpu_core/", "").replace("/", "")
                        try:
                            stats[clean_event] = int(val_str)
                        except ValueError:
                            pass

    # Clean up
    import shutil

    shutil.rmtree(tmp_dir)

    return stats


def format_number(n):
    return f"{n:,}".replace(",", ",")


def format_m(n):
    return f"{n / 1_000_000:.1f}M"


def main():
    baseline = parse_baseline()
    stats = run_benchmark()

    l1_misses = stats.get("L1-dcache-load-misses", 0)
    l1_loads = stats.get("L1-dcache-loads", 0)
    llc_misses = stats.get("LLC-load-misses", 0)
    llc_loads = stats.get("LLC-loads", 0)

    l1_misses_m = l1_misses / 1_000_000
    l1_misses_m = l1_misses / 1_000_000
    llc_loads_m = llc_loads / 1_000_000

    l1_rate = (l1_misses / l1_loads * 100) if l1_loads > 0 else 0

    evol_l1 = (
        ((l1_misses_m - baseline["l1_misses_m"]) / baseline["l1_misses_m"]) * 100
        if baseline["l1_misses_m"] > 0
        else 0
    )
    evol_llc_load = (
        ((llc_loads_m - baseline["llc_loads_m"]) / baseline["llc_loads_m"]) * 100
        if baseline["llc_loads_m"] > 0
        else 0
    )
    evol_llc_miss = (
        ((llc_misses - baseline["llc_misses"]) / baseline["llc_misses"]) * 100
        if baseline["llc_misses"] > 0
        else 0
    )

    today = (
        datetime.now(timezone.utc).strftime("%d %B %Y").replace("August", "Août")
    )  # basic french conversion
    new_iter = baseline["iter"] + 1

    frames = stats.get("frames", 1)

    l1_misses_per_frame = l1_misses / frames
    llc_loads_per_frame = llc_loads / frames
    llc_misses_per_frame = llc_misses / frames

    output = f"""## Itération {new_iter} : <Description> ({today})

<Texte Descriptif>

### Résultats Finaux (Validation Itération {new_iter} - <Feature>)

**Métriques Normalisées (Par Frame)** :
- **Frames générées (12s)** : {frames}
- **L1 Misses par Frame** : {l1_misses_per_frame:.0f} (Total: {l1_misses_m:.1f}M)
- **LLC Loads par Frame** : {llc_loads_per_frame:.0f} (Total: {llc_loads_m:.1f}M)
- **LLC Misses par Frame** : {llc_misses_per_frame:.0f} (Total: ~{format_m(llc_misses)})
- **Taux de Misses L1 global** : {l1_rate:.2f}%

**Comparatif (vs Baseline du {baseline["date"]} Itération {baseline["iter"]}) (Non Normalisé, indicatif)** :
- L1 Misses : {evol_l1:+.0f}% 
- LLC Loads : {evol_llc_load:+.0f}%
- LLC Misses : {evol_llc_miss:+.0f}%
"""
    print(output)


if __name__ == "__main__":
    main()
