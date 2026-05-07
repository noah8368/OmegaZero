#!/usr/bin/env python3
"""
ELO testing suite for OmegaZero.

Runs matches against Stockfish via cutechess-cli, records results to CSV,
and generates plots and summary tables.

Usage:
    python3 scripts/elo_test.py run [options]
    python3 scripts/elo_test.py plot [--input DIR]

Examples:
    python3 scripts/elo_test.py run --games 20 --tc 10+0.1
    python3 scripts/elo_test.py run --elo-levels 1400,1600,1800,2000 --games 50
    python3 scripts/elo_test.py plot --input build/elo_results
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

    if not shutil.which("cutechess-cli"):
        sys.exit(
            "cutechess-cli not found.\n"
            "Install: brew install cute-chess\n"
            "Or see: https://github.com/cutechess/cutechess"
        )

    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)

    levels = [int(x) for x in args.elo_levels.split(",")]
    all_games = []
    summary_rows = []
    global_game = 0

    for opp_elo in levels:
        print(f"\n{'=' * 60}")
        print(f"  Stockfish UCI_Elo {opp_elo}  |  "
              f"{args.games} games  |  tc={args.tc}")
        print(f"{'=' * 60}")

        cmd = [
            "cutechess-cli",
            "-engine", "name=OmegaZero", f"cmd={engine}",
                "arg=--uci", "proto=uci",
            "-engine", f"name=SF-{opp_elo}", f"cmd={sf}", "proto=uci",
                f"option.UCI_LimitStrength=true",
                f"option.UCI_Elo={opp_elo}",
            "-each", f"tc={args.tc}",
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
                  f"ELO est: {est:.0f}")

        proc.wait()
        stderr = proc.stderr.read()
        if stderr.strip():
            err_path = out / f"errors_{opp_elo}.log"
            err_path.write_text(stderr)

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

    print(f"\nResults saved to {out}/")
    print_summary(summary_rows)
    generate_plots(out)


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


def generate_plots(output_dir):
    output_dir = Path(output_dir)
    games_csv = output_dir / "games.csv"
    summary_csv = output_dir / "summary.csv"

    if not games_csv.exists() or not summary_csv.exists():
        print("No CSV data found. Run matches first.")
        return

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\nmatplotlib not installed — skipping plots.")
        print("Install with: pip3 install matplotlib")
        return

    with open(games_csv) as f:
        games = list(csv.DictReader(f))
    with open(summary_csv) as f:
        summary = list(csv.DictReader(f))

    if not games or not summary:
        print("No game data to plot.")
        return

    print("\nGenerating plots...")

    # --- Plot 1: Games vs running ELO estimate, one line per level ---
    fig, ax = plt.subplots(figsize=(10, 6))
    levels = sorted(set(g["opponent_elo"] for g in games))

    for level in levels:
        lg = [g for g in games if g["opponent_elo"] == level]
        x = [int(g["level_game"]) for g in lg]
        y = [float(g["running_elo"]) for g in lg]
        ax.plot(x, y, marker=".", markersize=4, label=f"vs SF {level}")

    ax.set_xlabel("Games Played at Level")
    ax.set_ylabel("Estimated ELO")
    ax.set_title("OmegaZero ELO Estimate Convergence")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = output_dir / "elo_convergence.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  {path}")

    # --- Plot 2: W/D/L grouped bar chart per level ---
    fig, ax = plt.subplots(figsize=(10, 6))

    x_labels = [r["opponent_elo"] for r in summary]
    w_vals = [int(r["wins"]) for r in summary]
    d_vals = [int(r["draws"]) for r in summary]
    l_vals = [int(r["losses"]) for r in summary]

    x = list(range(len(x_labels)))
    width = 0.25

    ax.bar([i - width for i in x], w_vals, width, label="Wins", color="#4CAF50")
    ax.bar(x, d_vals, width, label="Draws", color="#FFC107")
    ax.bar([i + width for i in x], l_vals, width, label="Losses", color="#F44336")

    ax.set_xlabel("Stockfish ELO Level")
    ax.set_ylabel("Games")
    ax.set_title("Win / Draw / Loss by Opponent Strength")
    ax.set_xticks(x)
    ax.set_xticklabels(x_labels)
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")
    fig.tight_layout()
    path = output_dir / "wdl_by_level.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"  {path}")

    # --- Plot 3: ELO estimate per opponent level ---
    fig, ax = plt.subplots(figsize=(10, 6))

    elos = [float(r["elo_estimate"]) for r in summary]
    ax.plot(x_labels, elos, "o-", color="#2196F3", markersize=8, linewidth=2)

    if elos:
        avg = sum(elos) / len(elos)
        ax.axhline(
            y=avg, color="#F44336", linestyle="--", alpha=0.7,
            label=f"Average: {avg:.0f}",
        )

    ax.set_xlabel("Stockfish ELO Level")
    ax.set_ylabel("OmegaZero Estimated ELO")
    ax.set_title("ELO Estimate by Opponent Strength")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = output_dir / "elo_by_level.png"
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
        "--elo-levels", default="1320,1500,1700,1900,2100",
        help="Comma-separated opponent ELO levels (default: 1320,1500,1700,1900,2100)",
    )
    run_p.add_argument(
        "--games", type=int, default=20,
        help="Games per level (default: 20)",
    )
    run_p.add_argument(
        "--tc", default="10+0.1",
        help="Time control, e.g. 10+0.1 (default: 10+0.1)",
    )
    run_p.add_argument(
        "--output", default="build/elo_results",
        help="Output directory (default: build/elo_results)",
    )

    plot_p = sub.add_parser("plot", help="Generate plots from existing results")
    plot_p.add_argument(
        "--input", default="build/elo_results",
        help="Directory with CSV results (default: build/elo_results)",
    )

    args = parser.parse_args()

    if args.command == "run":
        run_matches(args)
    elif args.command == "plot":
        summary_csv = Path(args.input) / "summary.csv"
        if summary_csv.exists():
            with open(summary_csv) as f:
                print_summary(list(csv.DictReader(f)))
        generate_plots(args.input)


if __name__ == "__main__":
    main()
