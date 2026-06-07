#!/usr/bin/env python3
"""
Plot search depth vs search time for all benchmark positions.

Runs bench_harness at increasing search durations and records the depth
reached per position. Produces a line plot showing how depth scales with
time for each of the four standard positions.

Usage:
    python3 scripts/depth_vs_time.py run                # default time points
    python3 scripts/depth_vs_time.py run --max-time 30   # go up to 30s
    python3 scripts/depth_vs_time.py plot results/depth_vs_time/<run>/data.csv
"""

import argparse
import csv
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RESULTS_BASE = REPO_ROOT / "results" / "depth_vs_time"
BENCH_BIN = REPO_ROOT / "build" / "bench_harness"

POSITIONS = ["opening", "midgame", "kiwipete", "endgame"]


def get_version_tag():
    try:
        tags = subprocess.check_output(
            ["git", "tag", "--points-at", "HEAD"],
            stderr=subprocess.DEVNULL, cwd=REPO_ROOT,
        ).decode().strip().splitlines()
        if tags:
            return tags[0]
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL, cwd=REPO_ROOT,
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def run_bench(search_time):
    proc = subprocess.run(
        [str(BENCH_BIN), str(search_time)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        cwd=REPO_ROOT,
    )
    if proc.returncode != 0:
        return None

    depths = []
    for line in proc.stderr.splitlines():
        m = re.match(r"SEARCH DEPTH:\s+(\d+)", line)
        if m:
            depths.append(int(m.group(1)))

    if len(depths) != len(POSITIONS):
        return None

    return dict(zip(POSITIONS, depths))


def generate_time_points(max_time):
    points = [0.1, 0.2, 0.5, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0, 20.0, 30.0]
    return [t for t in points if t <= max_time]


def cmd_run(args):
    if not BENCH_BIN.exists():
        sys.exit(f"Benchmark binary not found: {BENCH_BIN}\nRun 'make bench' first.")

    time_points = generate_time_points(args.max_time)
    total_time = sum(time_points) * len(POSITIONS)

    print(f"Running depth vs time benchmark")
    print(f"  Time points: {', '.join(f'{t}s' for t in time_points)}")
    print(f"  Positions: {', '.join(POSITIONS)}")
    print(f"  Estimated wall time: ~{total_time:.0f}s\n")

    rows = []
    for t in time_points:
        print(f"  {t:5.1f}s ...", end="", flush=True)
        result = run_bench(t)
        if result is None:
            print(" FAILED")
            continue

        depths_str = "  ".join(f"{p}: d{result[p]}" for p in POSITIONS)
        print(f"  {depths_str}")

        for pos in POSITIONS:
            rows.append({
                "search_time": t,
                "position": pos,
                "depth": result[pos],
            })

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    tag = get_version_tag()
    run_dir = RESULTS_BASE / f"{ts}_{tag}"
    run_dir.mkdir(parents=True, exist_ok=True)

    data_csv = run_dir / "data.csv"
    with open(data_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["search_time", "position", "depth"])
        w.writeheader()
        w.writerows(rows)

    print(f"\nData saved to {data_csv}")
    generate_plot(data_csv, run_dir)


def generate_plot(data_csv, output_dir=None):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("matplotlib/numpy not installed — skipping plot.")
        return

    data_csv = Path(data_csv)
    if output_dir is None:
        output_dir = data_csv.parent

    with open(data_csv) as f:
        rows = list(csv.DictReader(f))

    if not rows:
        print("No data to plot.")
        return

    positions = []
    seen = set()
    for r in rows:
        if r["position"] not in seen:
            positions.append(r["position"])
            seen.add(r["position"])

    colors = {
        "opening": "#1976D2",
        "midgame": "#7EC8A4",
        "kiwipete": "#D32F2F",
        "endgame": "#F4A7A0",
    }
    markers = {
        "opening": "o",
        "midgame": "s",
        "kiwipete": "D",
        "endgame": "^",
    }

    fig, ax = plt.subplots(figsize=(10, 6))

    for pos in positions:
        pos_rows = [r for r in rows if r["position"] == pos]
        times = [float(r["search_time"]) for r in pos_rows]
        depths = [int(r["depth"]) for r in pos_rows]

        color = colors.get(pos, "#333333")
        marker = markers.get(pos, "o")

        ax.plot(times, depths, marker=marker, color=color,
                linewidth=2, markersize=7, label=pos, zorder=5)

    ax.set_xscale("log")
    ax.set_xlabel("Search Time [s]", fontsize=12)
    ax.set_ylabel("Search Depth", fontsize=12)
    ax.set_title("Search Depth vs Time", fontsize=15,
                 fontweight="bold", pad=15)

    time_vals = sorted(set(float(r["search_time"]) for r in rows))
    ax.set_xticks(time_vals)
    ax.set_xticklabels([f"{t:g}s" for t in time_vals], fontsize=9)
    ax.minorticks_off()

    all_depths = [int(r["depth"]) for r in rows]
    ax.set_ylim(min(all_depths) - 1, max(all_depths) + 1)
    ax.yaxis.set_major_locator(plt.MaxNLocator(integer=True))

    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3, axis="y")

    fig.tight_layout()
    plot_path = Path(output_dir) / "depth_vs_time.png"
    fig.savefig(plot_path, dpi=150)
    plt.close(fig)
    print(f"  Plot saved: {plot_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Plot search depth vs search time for OmegaZero"
    )
    sub = parser.add_subparsers(dest="command")
    sub.required = True

    run_p = sub.add_parser("run", help="Run benchmark and generate plot")
    run_p.add_argument(
        "--max-time", type=float, default=20.0,
        help="Maximum search time in seconds (default: 20)",
    )

    plot_p = sub.add_parser("plot", help="Regenerate plot from data.csv")
    plot_p.add_argument("data_csv", help="Path to data.csv from a previous run")

    args = parser.parse_args()

    if args.command == "run":
        cmd_run(args)
    elif args.command == "plot":
        generate_plot(args.data_csv)


if __name__ == "__main__":
    main()
