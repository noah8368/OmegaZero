#!/usr/bin/env python3
"""
NPS benchmark runner for OmegaZero.

Runs the bench_harness binary, parses CSV output, saves results and plots
to nnue/results/benchmarking/<run_dir>/.

Usage:
    python3 scripts/benchmark.py run [--st 5]
    python3 scripts/benchmark.py plot
"""

import argparse
import csv
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RESULTS_BASE = REPO_ROOT / "results" / "benchmarking"
BENCH_BIN = REPO_ROOT / "build" / "bench_harness"


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
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL, cwd=REPO_ROOT,
        ).decode().strip()
        return commit
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def get_run_dir_name():
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    tag = get_version_tag()
    return f"{ts}_{tag}"


def run_benchmark(args):
    if not BENCH_BIN.exists():
        sys.exit(f"Benchmark binary not found: {BENCH_BIN}\nRun 'make bench' first.")

    run_name = get_run_dir_name()
    run_dir = RESULTS_BASE / run_name
    run_dir.mkdir(parents=True, exist_ok=True)

    print(f"Running benchmark ({args.st}s/position)...")
    proc = subprocess.run(
        [str(BENCH_BIN), str(args.st)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=REPO_ROOT,
    )

    if proc.stderr:
        print(proc.stderr, end="")

    if proc.returncode != 0:
        print(f"Benchmark failed (exit {proc.returncode})")
        sys.exit(1)

    lines = proc.stdout.strip().splitlines()
    if not lines:
        sys.exit("No output from benchmark")

    depths = []
    for line in proc.stderr.splitlines():
        m = re.match(r"SEARCH DEPTH:\s+(\d+)", line)
        if m:
            depths.append(int(m.group(1)))

    reader = csv.DictReader(lines)
    rows = list(reader)

    position_names = [r["position"] for r in rows if r["position"] != "average"]
    for i, name in enumerate(position_names):
        if i < len(depths):
            rows[i]["depth"] = depths[i]
        else:
            rows[i]["depth"] = 0
    for r in rows:
        r.setdefault("depth", 0)

    nps_csv = run_dir / "nps.csv"
    with open(nps_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["position", "nodes", "elapsed_s", "nps"])
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in ["position", "nodes", "elapsed_s", "nps"]})

    depths_csv = run_dir / "depths.csv"
    with open(depths_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["position", "depth"])
        w.writeheader()
        for i, name in enumerate(position_names):
            w.writerow({"position": name, "depth": depths[i] if i < len(depths) else 0})

    avg_row = next((r for r in rows if r["position"] == "average"), None)
    avg_nps = int(avg_row["nps"]) if avg_row else 0
    avg_knps = avg_nps // 1000

    print(f"\nResults for {run_name}:")
    print(f"  {'Position':<12} {'Depth':>6} {'NPS':>10}")
    print(f"  {'-'*12} {'-'*6} {'-'*10}")
    for row in rows:
        nps_str = f"{int(row['nps']):,}"
        print(f"  {row['position']:<12} {row['depth']:>6} {nps_str:>10}")
    print(f"\n  Average NPS: {avg_knps}k")

    version = get_version_tag()
    append_to_history(version, avg_knps)
    generate_plots(version)

    print(f"\n  Results saved to: {run_dir}")


def append_to_history(version, avg_knps):
    RESULTS_BASE.mkdir(parents=True, exist_ok=True)
    history_csv = RESULTS_BASE / "version_nps_history.csv"
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    file_exists = history_csv.exists()
    with open(history_csv, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["timestamp", "version", "avg_knps"])
        if not file_exists:
            w.writeheader()
        w.writerow({"timestamp": timestamp, "version": version, "avg_knps": avg_knps})

    print(f"  Version history: {history_csv}")


def load_history():
    history_csv = RESULTS_BASE / "version_nps_history.csv"
    if not history_csv.exists():
        return []
    with open(history_csv) as f:
        return list(csv.DictReader(f))


def generate_plots(current_version=None):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\nmatplotlib not installed — skipping plots.")
        return

    history = load_history()
    if not history:
        print("No history data for plots.")
        return

    versions = []
    knps_values = []
    for row in history:
        versions.append(row["version"])
        knps_values.append(int(row["avg_knps"]))

    RESULTS_BASE.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(max(8, len(versions) * 1.2), 5))
    colors = []
    for v in versions:
        if current_version and v == current_version:
            colors.append("#D32F2F")
        else:
            colors.append("#1976D2")

    bars = ax.bar(range(len(versions)), knps_values, color=colors,
                  edgecolor="none", width=0.6)
    for bar, nps in zip(bars, knps_values):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 8,
                f"{nps}k", ha="center", va="bottom", fontweight="bold", fontsize=11)

    ax.set_xlabel("Version")
    ax.set_ylabel("kNPS (thousands of nodes per second)")
    ax.set_title("OmegaZero — Nodes Per Second by Version")
    ax.set_xticks(range(len(versions)))
    ax.set_xticklabels(versions, rotation=45 if len(versions) > 6 else 0, ha="right")
    ax.set_ylim(0, max(knps_values) * 1.15)
    ax.grid(True, alpha=0.3, axis="y")
    fig.tight_layout()

    plot_path = RESULTS_BASE / "version_nps_plot.png"
    fig.savefig(plot_path, dpi=150)
    plt.close(fig)
    print(f"  NPS plot: {plot_path}")



def main():
    parser = argparse.ArgumentParser(description="OmegaZero NPS benchmark")
    sub = parser.add_subparsers(dest="command")
    sub.required = True

    run_p = sub.add_parser("run", help="Run the NPS benchmark")
    run_p.add_argument(
        "--st", type=float, default=5.0,
        help="Search time per position in seconds (default: 5.0)",
    )

    sub.add_parser("plot", help="Regenerate plots from version_nps_history.csv")

    args = parser.parse_args()

    if args.command == "run":
        run_benchmark(args)
    elif args.command == "plot":
        generate_plots(current_version=get_version_tag())


if __name__ == "__main__":
    main()
