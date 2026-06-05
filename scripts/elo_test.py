#!/usr/bin/env python3
"""
Elo testing suite for OmegaZero.

Estimates OmegaZero's playing strength by running matches against Stockfish
at various UCI_Elo levels via cutechess-cli. Records per-game results to
CSV, generates summary tables and plots.

Requires cutechess-cli and Stockfish (both auto-detected in PATH).

Subcommands:
    run   — Play matches against Stockfish at multiple Elo levels.
    plot  — Regenerate plots from version_history.csv.

Parameters (run subcommand):
    --elo-levels   Comma-separated Stockfish UCI_Elo levels to test against.
                   Default: 1320,1500,1700,1900,2100.
    --games        Number of games per Elo level (default: 20).
    --st           Fixed time per move in seconds (default: 0.1).
    --engine       Path to OmegaZero binary (default: build/OmegaZero).
    --cutechess    Path to cutechess-cli (default: auto-detect).

Results are saved to results/elo_testing/:
    <run_dir>/games.csv       — per-game results with running Elo estimates.
    <run_dir>/summary.csv     — W/D/L totals and Elo per opponent level.
    <run_dir>/games_{elo}.pgn — PGN files per Stockfish level.
    version_history.csv       — cumulative results across all versions.
    version_elo_plot.png      — Elo estimates by Stockfish level and version.

Usage:
    python3 scripts/elo_test.py run
    python3 scripts/elo_test.py run --elo-levels 1400,1600,1800,2000 --games 50 --st 0.5
    python3 scripts/elo_test.py plot
"""

import argparse
import csv
import math
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RESULTS_BASE = REPO_ROOT / "results" / "elo_testing"


def elo_diff(score_rate):
    score_rate = max(0.001, min(0.999, score_rate))
    return -400 * math.log10(1.0 / score_rate - 1.0)


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


def run_matches(args):
    engine = Path(args.engine)
    if not engine.is_absolute():
        engine = REPO_ROOT / engine
    engine = engine.resolve()
    if not engine.exists():
        sys.exit(f"Engine not found: {engine}\nRun 'make' first.")

    sf = args.stockfish
    if not Path(sf).exists() and not shutil.which(sf):
        sys.exit(f"Stockfish not found: {sf}\nInstall: brew install stockfish")

    cutechess = args.cutechess
    if cutechess is None:
        local_build = REPO_ROOT / "cutechess" / "build" / "cutechess-cli"
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

    run_name = get_run_dir_name()
    run_dir = RESULTS_BASE / run_name
    run_dir.mkdir(parents=True, exist_ok=True)

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
            "-pgnout", str(run_dir / f"games_{opp_elo}.pgn"),
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

    games_csv = run_dir / "games.csv"
    with open(games_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "game", "level_game", "opponent_elo", "omega_color",
            "result", "score", "running_elo",
        ])
        w.writeheader()
        w.writerows(all_games)

    summary_csv = run_dir / "summary.csv"
    with open(summary_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "opponent_elo", "games", "wins", "draws", "losses",
            "score_rate", "elo_estimate",
        ])
        w.writeheader()
        w.writerows(summary_rows)

    version = get_version_tag()
    print(f"\nResults saved to {run_dir}/  [version: {version}]")
    print_summary(summary_rows)
    append_to_history(summary_rows, version)
    generate_plots(version)


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


def append_to_history(summary_rows, version):
    RESULTS_BASE.mkdir(parents=True, exist_ok=True)
    history_csv = RESULTS_BASE / "version_history.csv"
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


def load_history():
    history_csv = RESULTS_BASE / "version_history.csv"
    if not history_csv.exists():
        return []
    with open(history_csv) as f:
        return list(csv.DictReader(f))


def generate_plots(current_version=None):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("\nmatplotlib not installed — skipping plots.")
        return

    history = load_history()
    if not history:
        print("No history data for plots.")
        return

    print("\nGenerating plots...")

    versions = []
    seen = set()
    for row in history:
        v = row["version"]
        if v not in seen:
            versions.append(v)
            seen.add(v)

    levels = sorted(set(int(row["opponent_elo"]) for row in history))

    RESULTS_BASE.mkdir(parents=True, exist_ok=True)

    n_versions = len(versions)
    n_levels = len(levels)
    n_groups = n_versions * n_levels

    fig, ax = plt.subplots(figsize=(max(10, n_groups * 0.9), 6))
    x = np.arange(n_groups)

    win_rates = []
    draw_rates = []
    loss_rates = []
    labels = []
    elo_by_version = {}

    for ver in versions:
        elo_estimates = []
        for level in levels:
            matches = [r for r in history
                       if r["version"] == ver
                       and int(r["opponent_elo"]) == level]
            if matches:
                row = matches[-1]
                w = int(row["wins"])
                d = int(row["draws"])
                lo = int(row["losses"])
                total = w + d + lo
                win_rates.append(w / total if total else 0)
                draw_rates.append(d / total if total else 0)
                loss_rates.append(lo / total if total else 0)
                elo_estimates.append(float(row["elo_estimate"]))
            else:
                win_rates.append(0)
                draw_rates.append(0)
                loss_rates.append(0)
            labels.append(f"{ver}\nSF {level}")
        if elo_estimates:
            elo_by_version[ver] = round(sum(elo_estimates) / len(elo_estimates))

    w_arr = np.array(win_rates)
    d_arr = np.array(draw_rates)
    lo_arr = np.array(loss_rates)

    ax.bar(x, w_arr, width=0.7, label="Win", color="#7EC8A4")
    ax.bar(x, d_arr, width=0.7, bottom=w_arr, label="Draw", color="#A8D8EA")
    ax.bar(x, lo_arr, width=0.7, bottom=w_arr + d_arr, label="Loss",
           color="#F4A7A0")

    ax.set_ylabel("Fraction")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=8, ha="center")
    ax.set_ylim(0, 1.0)
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3, axis="y")

    # Add vertical separators and Elo annotations between version groups.
    for i, ver in enumerate(versions):
        group_start = i * n_levels
        group_end = group_start + n_levels - 1
        group_center = (group_start + group_end) / 2
        if ver in elo_by_version:
            ax.text(group_center, 1.06, f"~{elo_by_version[ver]} Elo",
                    ha="center", va="bottom", fontsize=9, fontweight="bold",
                    transform=ax.get_xaxis_transform())
        if i < n_versions - 1:
            sep_x = group_end + 0.5
            ax.axvline(sep_x, color="#999999", linewidth=0.8, linestyle="--")

    fig.suptitle("OmegaZero — W/D/L Ratio by Version and Stockfish Level",
                 fontsize=13, y=0.99)
    fig.tight_layout(rect=[0, 0, 1, 0.90])

    plot_path = RESULTS_BASE / "version_elo_plot.png"
    fig.savefig(plot_path, dpi=150)
    plt.close(fig)
    print(f"  Elo plot: {plot_path}")



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
        help="Path to cutechess-cli (default: auto-detect)",
    )
    run_p.add_argument(
        "--elo-levels", default="1320,1700,2100",
        help="Comma-separated opponent ELO levels (default: 1320,1700,2100)",
    )
    run_p.add_argument(
        "--games", type=int, default=20,
        help="Games per level (default: 20)",
    )
    run_p.add_argument(
        "--st", default="5",
        help="Fixed time per move in seconds (default: 5)",
    )

    sub.add_parser("plot", help="Regenerate plots from version_history.csv")

    args = parser.parse_args()

    if args.command == "run":
        run_matches(args)
    elif args.command == "plot":
        generate_plots(current_version=get_version_tag())


if __name__ == "__main__":
    main()
