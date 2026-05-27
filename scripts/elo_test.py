#!/usr/bin/env python3
"""
ELO testing suite for OmegaZero.

Runs matches against Stockfish via cutechess-cli, records results to CSV,
and generates plots and summary tables.

Usage:
    python3 scripts/elo_test.py run [options]
    python3 scripts/elo_test.py plot [--input DIR]

Examples:
    python3 scripts/elo_test.py run --games 20 --st 0.5
    python3 scripts/elo_test.py run --elo-levels 1400,1600,1800,2000 --games 50
    python3 scripts/elo_test.py plot --input results
"""

import argparse
import csv
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def elo_diff(score_rate):
    score_rate = max(0.001, min(0.999, score_rate))
    return -400 * math.log10(1.0 / score_rate - 1.0)


def run_matches(args):
    engine = Path(args.engine).resolve()
    if not engine.exists():
        sys.exit(f"Engine not found: {engine}\nRun 'make' first.")

    sf = args.stockfish
    if not Path(sf).exists() and not shutil.which(sf):
        sys.exit(f"Stockfish not found: {sf}\nInstall: brew install stockfish")

    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    cutechess = args.cutechess
    if cutechess is None:
        local_build = project_root / "cutechess" / "build" / "cutechess-cli"
        if local_build.exists():
            cutechess = str(local_build)
        elif shutil.which("cutechess-cli"):
            cutechess = "cutechess-cli"
        else:
            sys.exit(
                "cutechess-cli not found.\n"
                "Looked in: cutechess/build/cutechess-cli and PATH.\n"
                "Use --cutechess to specify the path."
            )
    elif not Path(cutechess).exists() and not shutil.which(cutechess):
        sys.exit(f"cutechess-cli not found: {cutechess}")

    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)

    levels = [int(x) for x in args.elo_levels.split(",")]
    all_games = []
    summary_rows = []
    global_game = 0

    for opp_elo in levels:
        print(f"\n{'=' * 60}", flush=True)
        print(f"  Stockfish UCI_Elo {opp_elo}  |  "
              f"{args.games} games  |  {args.st}s/move", flush=True)
        print(f"{'=' * 60}", flush=True)

        cmd = [
            cutechess,
            "-engine", "name=OmegaZero", f"cmd={engine}",
                "arg=--uci", "proto=uci",
            "-engine", f"name=SF-{opp_elo}", f"cmd={sf}", "proto=uci",
                f"option.UCI_LimitStrength=true",
                f"option.UCI_Elo={opp_elo}",
            "-each", f"st={args.st}", "timemargin=500",
            "-rounds", str(args.games),
            "-pgnout", str(out / f"games_{opp_elo}.pgn"),
            "-recover",
            "-repeat",
        ]

        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

        wins, draws, losses = 0, 0, 0
        level_games = []

        for line in proc.stdout:
            line = line.strip()
            m = re.match(
                r"Finished game (\d+) \((.+?) vs (.+?)\): (\S+)", line
            )
            if not m:
                continue

            game_num = int(m.group(1))
            white = m.group(2)
            result = m.group(4)
            omega_white = "OmegaZero" in white

            if result == "1-0":
                score = 1.0 if omega_white else 0.0
            elif result == "0-1":
                score = 0.0 if omega_white else 1.0
            else:
                score = 0.5

            if score == 1.0:
                wins += 1
            elif score == 0.0:
                losses += 1
            else:
                draws += 1

            total = wins + draws + losses
            rate = (wins + 0.5 * draws) / total
            est = opp_elo + elo_diff(rate)
            global_game += 1

            level_games.append({
                "game": global_game,
                "level_game": total,
                "opponent_elo": opp_elo,
                "omega_color": "white" if omega_white else "black",
                "result": result,
                "score": score,
                "running_elo": round(est, 1),
            })

            tag = "Win " if score == 1.0 else ("Draw" if score == 0.5 else "Loss")
            print(f"  {total:3d}/{args.games}  {tag}  "
                  f"W:{wins} D:{draws} L:{losses}  "
                  f"ELO est: {est:.0f}", flush=True)

        proc.wait()
        proc.stderr.read()

        all_games.extend(level_games)

        total = wins + draws + losses
        if total > 0:
            rate = (wins + 0.5 * draws) / total
            est = opp_elo + elo_diff(rate)
            summary_rows.append({
                "opponent_elo": opp_elo,
                "games": total,
                "wins": wins,
                "draws": draws,
                "losses": losses,
                "score_rate": round(rate, 3),
                "elo_estimate": round(est, 1),
            })

    games_csv = out / "games.csv"
    with open(games_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "game", "level_game", "opponent_elo", "omega_color",
            "result", "score", "running_elo",
        ])
        w.writeheader()
        w.writerows(all_games)

    summary_csv = out / "summary.csv"
    with open(summary_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "opponent_elo", "games", "wins", "draws", "losses",
            "score_rate", "elo_estimate",
        ])
        w.writeheader()
        w.writerows(summary_rows)

    version = get_version_tag()
    print(f"\nResults saved to {out}/  [version: {version}]")
    print_summary(summary_rows)
    append_to_history(summary_rows, version, out)
    generate_plots(out, version=version)


def print_summary(rows):
    if not rows:
        print("No results to summarize.")
        return

    print(f"\n{'=' * 70}")
    print("  SUMMARY")
    print(f"{'=' * 70}")
    print(f"  {'Opp ELO':>8}  {'Games':>5}  "
          f"{'W':>3}  {'D':>3}  {'L':>3}  "
          f"{'Score':>6}  {'Est. ELO':>9}")
    print(f"  {'--------':>8}  {'-----':>5}  "
          f"{'---':>3}  {'---':>3}  {'---':>3}  "
          f"{'------':>6}  {'---------':>9}")

    elo_estimates = []
    for r in rows:
        print(f"  {r['opponent_elo']:>8}  {r['games']:>5}  "
              f"{r['wins']:>3}  {r['draws']:>3}  {r['losses']:>3}  "
              f"{float(r['score_rate']):>6.3f}  "
              f"{float(r['elo_estimate']):>9.1f}")
        elo_estimates.append(float(r["elo_estimate"]))

    if elo_estimates:
        avg = sum(elo_estimates) / len(elo_estimates)
        print(f"\n  Average estimated ELO: {avg:.0f}")
    print(f"{'=' * 70}")


def get_version_tag():
    """Get a version tag from git (short hash + dirty flag)."""
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
        dirty = subprocess.call(
            ["git", "diff", "--quiet"],
            stderr=subprocess.DEVNULL
        ) != 0
        return commit + ("-dirty" if dirty else "")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def append_to_history(summary_rows, version, output_dir):
    """Append current run results to the version history CSV."""
    history_csv = Path(output_dir) / "version_history.csv"
    from datetime import datetime
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    file_exists = history_csv.exists()
    with open(history_csv, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "timestamp", "version", "opponent_elo", "games",
            "wins", "draws", "losses", "win_rate", "elo_estimate",
        ])
        if not file_exists:
            w.writeheader()
        for r in summary_rows:
            total = int(r["games"])
            win_rate = float(r["score_rate"])
            w.writerow({
                "timestamp": timestamp,
                "version": version,
                "opponent_elo": r["opponent_elo"],
                "games": total,
                "wins": r["wins"],
                "draws": r["draws"],
                "losses": r["losses"],
                "win_rate": round(win_rate, 3),
                "elo_estimate": r["elo_estimate"],
            })
    print(f"  Version history: {history_csv}")


def generate_plots(output_dir, version=None):
    output_dir = Path(output_dir)
    history_csv = output_dir / "version_history.csv"
    benchmark_csv = output_dir / "v4_benchmark.csv"

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("\nmatplotlib not installed — skipping plots.")
        print("Install with: pip3 install matplotlib")
        return

    print("\nGenerating plots...")

    # --- Plot 1: NPS by version (bar chart) ---
    nps_data = {"v1": 197, "v2": 507, "v3": 498, "v4": 406}
    if benchmark_csv.exists():
        with open(benchmark_csv) as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row["Position"].lower() == "average":
                    last_col = [k for k in row.keys() if "[kNPS]" in k][-1]
                    nps_data["v4"] = int(row[last_col])
                    break

    versions = sorted(nps_data.keys(), key=lambda v: int(v[1:]))
    nps_values = [nps_data[v] for v in versions]

    fig, ax = plt.subplots(figsize=(8, 5))
    colors = ["#1976D2", "#388E3C", "#F57C00", "#D32F2F"]
    bars = ax.bar(versions, nps_values, color=colors[:len(versions)],
                  edgecolor="none", width=0.6)
    for bar, nps in zip(bars, nps_values):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 8,
                f"{nps}k", ha="center", va="bottom", fontweight="bold", fontsize=11)

    ax.set_xlabel("Version")
    ax.set_ylabel("kNPS (thousands of nodes per second)")
    ax.set_title("OmegaZero — Nodes Per Second by Version")
    ax.set_ylim(0, max(nps_values) * 1.15)
    ax.grid(True, alpha=0.3, axis="y")
    fig.tight_layout()
    path = output_dir / "version_nps_plot.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  {path}")

    # --- Plot 2: ELO by version and Stockfish level (grouped bars) ---
    if history_csv.exists():
        with open(history_csv) as f:
            history = list(csv.DictReader(f))

        if history:
            versions = []
            seen = set()
            for row in history:
                v = row["version"]
                if v not in seen:
                    versions.append(v)
                    seen.add(v)

            levels = sorted(set(int(row["opponent_elo"]) for row in history))

            fig, ax = plt.subplots(figsize=(10, 6))
            x = np.arange(len(levels))
            width = 0.8 / max(1, len(versions))
            colors = ["#1976D2", "#388E3C", "#F57C00", "#D32F2F", "#7B1FA2"]

            for i, ver in enumerate(versions):
                elos = []
                for level in levels:
                    matches = [r for r in history
                               if r["version"] == ver
                               and int(r["opponent_elo"]) == level]
                    if matches:
                        elos.append(float(matches[-1]["elo_estimate"]))
                    else:
                        elos.append(0)
                offset = (i - len(versions) / 2 + 0.5) * width
                ax.bar(x + offset, elos, width, label=ver,
                       color=colors[i % len(colors)], alpha=0.85)

            ax.set_xlabel("Stockfish Level")
            ax.set_ylabel("Estimated ELO")
            ax.set_title("OmegaZero — ELO Estimate by Stockfish Level and Version")
            ax.set_xticks(x)
            ax.set_xticklabels([f"SF {l}" for l in levels])
            ax.legend(title="Version")
            ax.grid(True, alpha=0.3, axis="y")
            fig.tight_layout()
            path = output_dir / "version_elo_plot.png"
            fig.savefig(path, dpi=150)
            plt.close(fig)
            print(f"  {path}")


def main():
    parser = argparse.ArgumentParser(
        description="OmegaZero ELO testing suite"
    )
    sub = parser.add_subparsers(dest="command")
    sub.required = True

    run_p = sub.add_parser("run", help="Run matches against Stockfish")
    run_p.add_argument(
        "--engine", default="build/OmegaZero",
        help="Path to OmegaZero binary (default: build/OmegaZero)",
    )
    run_p.add_argument(
        "--stockfish", default="stockfish",
        help="Path or command for Stockfish (default: stockfish)",
    )
    run_p.add_argument(
        "--cutechess", default=None,
        help="Path to cutechess-cli (default: auto-detect from cutechess/build/ or PATH)",
    )
    run_p.add_argument(
        "--elo-levels", default="1320,1500,1700,1900,2100",
        help="Comma-separated opponent ELO levels (default: 1320,1500,1700,1900,2100)",
    )
    run_p.add_argument(
        "--games", type=int, default=20,
        help="Games per level (default: 20)",
    )
    run_p.add_argument(
        "--st", default="0.1",
        help="Fixed time per move in seconds (default: 0.1)",
    )
    run_p.add_argument(
        "--output", default="results",
        help="Output directory (default: results)",
    )

    plot_p = sub.add_parser("plot", help="Generate plots from existing results")
    plot_p.add_argument(
        "--input", default="results",
        help="Directory with CSV results (default: results)",
    )

    args = parser.parse_args()

    if args.command == "run":
        run_matches(args)
    elif args.command == "plot":
        generate_plots(args.input, version=get_version_tag())


if __name__ == "__main__":
    main()
