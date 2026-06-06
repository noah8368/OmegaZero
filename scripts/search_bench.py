#!/usr/bin/env python3
"""
NPS (nodes per second) search benchmark for OmegaZero.

Measures search performance by running the bench_harness binary across
four standard positions (opening, midgame, kiwipete, endgame) for a fixed
duration. Reports NPS and search depth per position plus an overall average.

Subcommands:
    run       — Benchmark the current build (requires 'make bench' first).
    gauntlet  — Benchmark all tagged versions (v1, v2, ..., vN).
                Builds each from a git worktree automatically.
    plot      — Regenerate the NPS bar chart from version_nps_history.csv.

Parameters:
    --st       Search time per position in seconds (default: 5.0).
    --resume   (gauntlet only) Skip versions already in the history CSV.

Results are saved to results/benchmarking/:
    <run_dir>/nps.csv             — per-position NPS, nodes, and elapsed time.
    <run_dir>/depths.csv          — search depth reached per position.
    version_nps_history.csv       — cumulative average NPS across all versions.
    version_nps_plot.png          — NPS bar chart across versions.

Usage:
    python3 scripts/search_bench.py run               # 5s/position (default)
    python3 scripts/search_bench.py run --st 10        # 10s for more stable results
    python3 scripts/search_bench.py gauntlet           # benchmark all tagged versions
    python3 scripts/search_bench.py gauntlet --resume  # skip already-done versions
    python3 scripts/search_bench.py plot               # regenerate plots
"""

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RESULTS_BASE = REPO_ROOT / "results" / "benchmarking"
HISTORY_CSV = RESULTS_BASE / "version_nps_history.csv"
BENCH_BIN = REPO_ROOT / "build" / "bench_harness"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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


def get_commit_short(ref):
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", ref],
            stderr=subprocess.DEVNULL, cwd=REPO_ROOT,
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        sys.exit(f"Could not resolve git ref: {ref}")


def discover_version_tags():
    try:
        raw = subprocess.check_output(
            ["git", "tag", "-l", "v*"],
            stderr=subprocess.DEVNULL, cwd=REPO_ROOT,
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []

    numeric = []
    other = []
    for line in raw.splitlines():
        tag = line.strip()
        suffix = tag[1:]
        if suffix.isdigit():
            numeric.append((int(suffix), tag))
        else:
            other.append(tag)

    numeric.sort()
    other.sort()
    return [tag for _, tag in numeric] + other


def build_bench_at_commit(commit_ref, label):
    commit_hash = get_commit_short(commit_ref)
    print(f"  Building {label} ({commit_hash})...", flush=True)

    tmpdir = tempfile.mkdtemp(prefix=f"omega_bench_{label}_")
    worktree = Path(tmpdir) / "worktree"

    try:
        subprocess.check_call(
            ["git", "worktree", "add", "--detach", str(worktree), commit_ref],
            cwd=REPO_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        shutil.rmtree(tmpdir, ignore_errors=True)
        sys.exit(f"Failed to create git worktree at {commit_ref}")

    for d in ["build", "build/bench"]:
        (worktree / d).mkdir(parents=True, exist_ok=True)

    makefile = worktree / "Makefile"
    if makefile.exists():
        txt = makefile.read_text()
        txt = txt.replace("-Werror", "")
        if "magics.o: src/magics.cc" not in txt:
            m = re.search(r"(build(?:/\w+)?)/magics\.o", txt)
            if m:
                prefix = m.group(1)
                txt += (f"\n{prefix}/magics.o: src/magics.cc\n"
                        f"\t$(CC) -c -o $@ $< $(FLAGS) -O0\n")
        makefile.write_text(txt)

    try:
        subprocess.check_call(
            ["make", "-j", str(os.cpu_count() or 4), "bench"],
            cwd=worktree, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as e:
        cleanup_worktree(worktree)
        shutil.rmtree(tmpdir, ignore_errors=True)
        sys.exit(f"Build failed at {commit_hash}:\n"
                 f"{e.stderr.decode() if e.stderr else ''}")

    binary = worktree / "build" / "bench_harness"
    if not binary.exists():
        cleanup_worktree(worktree)
        shutil.rmtree(tmpdir, ignore_errors=True)
        sys.exit(f"bench_harness not found after build at {commit_hash}")

    print(f"  Built {commit_hash} -> {binary}", flush=True)
    return str(binary), tmpdir, str(worktree)


def cleanup_worktree(worktree_path):
    try:
        subprocess.check_call(
            ["git", "worktree", "remove", "--force", str(worktree_path)],
            cwd=REPO_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass


def run_bench_binary(binary_path, search_time):
    """Run a bench_harness binary and return (rows, depths, avg_knps)."""
    proc = subprocess.run(
        [binary_path, str(search_time)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        cwd=REPO_ROOT,
    )

    if proc.stderr:
        for line in proc.stderr.splitlines():
            if "SEARCH DEPTH:" in line:
                print(f"    {line.strip()}", flush=True)

    if proc.returncode != 0:
        return None, None, 0

    lines = proc.stdout.strip().splitlines()
    if not lines:
        return None, None, 0

    depths = []
    for line in proc.stderr.splitlines():
        m = re.match(r"SEARCH DEPTH:\s+(\d+)", line)
        if m:
            depths.append(int(m.group(1)))

    reader = csv.DictReader(lines)
    rows = list(reader)

    position_names = [r["position"] for r in rows if r["position"] != "average"]
    for i, name in enumerate(position_names):
        rows[i]["depth"] = depths[i] if i < len(depths) else 0
    for r in rows:
        r.setdefault("depth", 0)

    avg_row = next((r for r in rows if r["position"] == "average"), None)
    avg_nps = int(avg_row["nps"]) if avg_row else 0
    avg_knps = avg_nps // 1000

    return rows, depths, avg_knps


# ---------------------------------------------------------------------------
# History
# ---------------------------------------------------------------------------

def append_to_history(version, avg_knps):
    RESULTS_BASE.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    file_exists = HISTORY_CSV.exists()
    with open(HISTORY_CSV, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["timestamp", "version", "avg_knps"])
        if not file_exists:
            w.writeheader()
        w.writerow({"timestamp": timestamp, "version": version,
                     "avg_knps": avg_knps})


def load_history():
    if not HISTORY_CSV.exists():
        return []
    with open(HISTORY_CSV) as f:
        return list(csv.DictReader(f))


def completed_versions():
    return {r["version"] for r in load_history()}


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

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
                f"{nps}k", ha="center", va="bottom", fontweight="bold",
                fontsize=11)

    ax.set_xlabel("Version")
    ax.set_ylabel("kNPS (thousands of nodes per second)")
    ax.set_title("OmegaZero — Nodes Per Second by Version")
    ax.set_xticks(range(len(versions)))
    ax.set_xticklabels(versions,
                       rotation=45 if len(versions) > 6 else 0, ha="right")
    ax.set_ylim(0, max(knps_values) * 1.15)
    ax.grid(True, alpha=0.3, axis="y")
    fig.tight_layout()

    plot_path = RESULTS_BASE / "version_nps_plot.png"
    fig.savefig(plot_path, dpi=150)
    plt.close(fig)
    print(f"  NPS plot: {plot_path}")


# ---------------------------------------------------------------------------
# run subcommand
# ---------------------------------------------------------------------------

def cmd_run(args):
    if not BENCH_BIN.exists():
        sys.exit(f"Benchmark binary not found: {BENCH_BIN}\n"
                 f"Run 'make bench' first.")

    run_name = f"{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}_{get_version_tag()}"
    run_dir = RESULTS_BASE / run_name
    run_dir.mkdir(parents=True, exist_ok=True)

    print(f"Running benchmark ({args.st}s/position)...")
    rows, depths, avg_knps = run_bench_binary(str(BENCH_BIN), args.st)

    if rows is None:
        sys.exit("Benchmark failed")

    position_names = [r["position"] for r in rows if r["position"] != "average"]

    nps_csv = run_dir / "nps.csv"
    with open(nps_csv, "w", newline="") as f:
        w = csv.DictWriter(f,
                           fieldnames=["position", "nodes", "elapsed_s", "nps"])
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in
                        ["position", "nodes", "elapsed_s", "nps"]})

    depths_csv = run_dir / "depths.csv"
    with open(depths_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["position", "depth"])
        w.writeheader()
        for i, name in enumerate(position_names):
            w.writerow({"position": name,
                         "depth": depths[i] if i < len(depths) else 0})

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


# ---------------------------------------------------------------------------
# gauntlet subcommand
# ---------------------------------------------------------------------------

def cmd_gauntlet(args):
    tags = discover_version_tags()
    if not tags:
        sys.exit("No version tags found (v1, v2, ...)")

    done = completed_versions() if args.resume else set()

    print(f"\n{'=' * 64}")
    print(f"  OmegaZero Search Benchmark Gauntlet")
    print(f"  Tags: {', '.join(tags)}")
    print(f"  {args.st}s/position")
    print(f"{'=' * 64}\n")

    for i, tag in enumerate(tags):
        if tag in done:
            print(f"[{i+1}/{len(tags)}] {tag} — already done, skipping\n")
            continue

        print(f"[{i+1}/{len(tags)}] {tag}")

        binary, tmpdir, worktree = build_bench_at_commit(tag, tag)

        print(f"  Benchmarking ({args.st}s/position)...", flush=True)
        rows, depths, avg_knps = run_bench_binary(binary, args.st)

        cleanup_worktree(worktree)
        shutil.rmtree(tmpdir, ignore_errors=True)

        if rows is None:
            print(f"  >> {tag}: FAILED\n")
            continue

        print(f"\n  {'Position':<12} {'Depth':>6} {'NPS':>10}")
        print(f"  {'-'*12} {'-'*6} {'-'*10}")
        for row in rows:
            nps_str = f"{int(row['nps']):,}"
            print(f"  {row['position']:<12} {row['depth']:>6} {nps_str:>10}")
        print(f"\n  >> {tag}: {avg_knps}k NPS\n")

        append_to_history(tag, avg_knps)

    print(f"{'=' * 64}")
    print(f"  Done! Results saved to {HISTORY_CSV}")
    print(f"{'=' * 64}")

    generate_plots()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="OmegaZero NPS search benchmark")
    sub = parser.add_subparsers(dest="command")

    run_p = sub.add_parser("run", help="Benchmark the current build")
    run_p.add_argument(
        "--st", type=float, default=5.0,
        help="Search time per position in seconds (default: 5.0)",
    )

    gauntlet_p = sub.add_parser(
        "gauntlet",
        help="Benchmark all tagged versions (v1, v2, ..., vN)")
    gauntlet_p.add_argument(
        "--st", type=float, default=5.0,
        help="Search time per position in seconds (default: 5.0)",
    )
    gauntlet_p.add_argument(
        "--resume", action="store_true",
        help="Skip versions already in version_nps_history.csv",
    )

    sub.add_parser("plot",
                    help="Regenerate plots from version_nps_history.csv")

    args = parser.parse_args()

    if args.command is None:
        parser.print_help()
        sys.exit(1)

    if args.command == "run":
        cmd_run(args)
    elif args.command == "gauntlet":
        cmd_gauntlet(args)
    elif args.command == "plot":
        generate_plots(current_version=get_version_tag())


if __name__ == "__main__":
    main()
