#!/usr/bin/env python3
"""
Elo estimation for OmegaZero via logistic curve fitting.

Runs OmegaZero against Stockfish at multiple UCI_Elo levels, then fits the
standard Elo logistic model P = 1/(1 + 10^((R_opp - R_engine)/400)) to
estimate OmegaZero's rating with bootstrap confidence intervals.

Subcommands:
    run   — Play matches and generate the Elo estimation plot.
    plot  — Regenerate the plot from an existing run's summary.csv.

Usage:
    python3 scripts/elo.py run
    python3 scripts/elo.py run --games 50 --st 0.5      # quick test
    python3 scripts/elo.py run --elo-levels 1700,1800,1900,2000,2100,2200,2300
    python3 scripts/elo.py plot results/elo/2026-06-06_.../summary.csv
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
RESULTS_BASE = REPO_ROOT / "results" / "elo"


def elo_expected(r_opp, r_engine):
    return 1.0 / (1.0 + 10.0 ** ((r_opp - r_engine) / 400.0))


def neg_log_likelihood(r_engine, opp_elos, score_rates, game_counts):
    nll = 0.0
    for r_opp, sr, n in zip(opp_elos, score_rates, game_counts):
        p = elo_expected(r_opp, r_engine)
        p = max(1e-12, min(1 - 1e-12, p))
        nll -= n * (sr * math.log(p) + (1 - sr) * math.log(1 - p))
    return nll


def fit_elo(opp_elos, score_rates, game_counts):
    lo, hi = 500.0, 3500.0
    for _ in range(200):
        m1 = lo + (hi - lo) / 3
        m2 = hi - (hi - lo) / 3
        if neg_log_likelihood(m1, opp_elos, score_rates, game_counts) < \
           neg_log_likelihood(m2, opp_elos, score_rates, game_counts):
            hi = m2
        else:
            lo = m1
    return (lo + hi) / 2


def bootstrap_elo(wins, draws, losses, opp_elos, n_boot=5000):
    import numpy as np
    rng = np.random.default_rng(42)

    n_levels = len(opp_elos)
    game_counts = [w + d + lo for w, d, lo in zip(wins, draws, losses)]

    boot_elos = []
    for _ in range(n_boot):
        boot_rates = []
        for i in range(n_levels):
            n = game_counts[i]
            outcomes = np.concatenate([
                np.ones(wins[i]),
                np.full(draws[i], 0.5),
                np.zeros(losses[i]),
            ])
            sample = rng.choice(outcomes, size=n, replace=True)
            boot_rates.append(sample.mean())

        r = fit_elo(opp_elos, boot_rates, game_counts)
        boot_elos.append(r)

    boot_elos = np.array(boot_elos)
    return boot_elos


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

    levels = [int(x) for x in args.elo_levels.split(",")]
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    tag = get_version_tag()
    run_dir = RESULTS_BASE / f"{ts}_{tag}"
    run_dir.mkdir(parents=True, exist_ok=True)

    summary_rows = []

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
                "option.UCI_LimitStrength=true",
                f"option.UCI_Elo={opp_elo}",
            "-each", f"st={args.st}", "timemargin=500",
            "-rounds", str(args.games),
            "-concurrency", str(args.concurrency),
            "-pgnout", str(run_dir / f"games_{opp_elo}.pgn"),
            "-recover",
            "-repeat",
        ]

        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

        wins, draws, losses = 0, 0, 0
        for line in proc.stdout:
            line = line.strip()
            m = re.match(
                r"Finished game (\d+) \((.+?) vs (.+?)\): (\S+)", line
            )
            if not m:
                continue

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
            tag_str = "Win " if score == 1.0 else ("Draw" if score == 0.5 else "Loss")
            rate = (wins + 0.5 * draws) / total
            print(f"  {total:3d}/{args.games}  {tag_str}  "
                  f"W:{wins} D:{draws} L:{losses}  "
                  f"Score: {rate:.3f}", flush=True)

        proc.wait()
        proc.stderr.read()

        total = wins + draws + losses
        if total > 0:
            rate = (wins + 0.5 * draws) / total
            summary_rows.append({
                "opponent_elo": opp_elo,
                "games": total,
                "wins": wins,
                "draws": draws,
                "losses": losses,
                "score_rate": round(rate, 4),
            })

    summary_csv = run_dir / "summary.csv"
    with open(summary_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "opponent_elo", "games", "wins", "draws", "losses", "score_rate",
        ])
        w.writeheader()
        w.writerows(summary_rows)

    print(f"\nResults saved to {run_dir}/")
    print_summary(summary_rows)
    generate_plot(summary_csv, run_dir)


def print_summary(rows):
    if not rows:
        print("No results.")
        return

    print(f"\n{'=' * 60}")
    print(f"  {'Opp ELO':>8}  {'Games':>5}  "
          f"{'W':>4}  {'D':>4}  {'L':>4}  {'Score':>7}")
    print(f"  {'--------':>8}  {'-----':>5}  "
          f"{'----':>4}  {'----':>4}  {'----':>4}  {'-------':>7}")
    for r in rows:
        print(f"  {r['opponent_elo']:>8}  {r['games']:>5}  "
              f"{r['wins']:>4}  {r['draws']:>4}  {r['losses']:>4}  "
              f"{float(r['score_rate']):>7.4f}")
    print(f"{'=' * 60}")


def generate_plot(summary_csv, output_dir=None):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("matplotlib/numpy not installed — skipping plot.")
        return

    summary_csv = Path(summary_csv)
    if output_dir is None:
        output_dir = summary_csv.parent

    with open(summary_csv) as f:
        rows = list(csv.DictReader(f))

    if not rows:
        print("No data to plot.")
        return

    opp_elos = [int(r["opponent_elo"]) for r in rows]
    game_counts = [int(r["games"]) for r in rows]
    wins_list = [int(r["wins"]) for r in rows]
    draws_list = [int(r["draws"]) for r in rows]
    losses_list = [int(r["losses"]) for r in rows]
    score_rates = [float(r["score_rate"]) for r in rows]

    # --- Error bars from W/D/L ---
    means = np.array(score_rates)
    se_list = []
    for i in range(len(rows)):
        w, d, lo, n = wins_list[i], draws_list[i], losses_list[i], game_counts[i]
        mu = means[i]
        var = (w * (1.0 - mu) ** 2 +
               d * (0.5 - mu) ** 2 +
               lo * (0.0 - mu) ** 2) / max(n - 1, 1)
        se_list.append(math.sqrt(var / n))
    se_arr = np.array(se_list)
    ci_lo = np.clip(means - 1.96 * se_arr, 0, 1)
    ci_hi = np.clip(means + 1.96 * se_arr, 0, 1)

    # --- Fit Elo ---
    r_engine = fit_elo(opp_elos, score_rates, game_counts)

    # --- Bootstrap ---
    print("Running bootstrap (5000 resamples)...", flush=True)
    boot_elos = bootstrap_elo(wins_list, draws_list, losses_list, opp_elos)
    elo_median = float(np.median(boot_elos))
    elo_lo = float(np.percentile(boot_elos, 2.5))
    elo_hi = float(np.percentile(boot_elos, 97.5))
    elo_half_ci = round((elo_hi - elo_lo) / 2)

    print(f"\n  Estimated Elo: {elo_median:.0f} ± {elo_half_ci} "
          f"(95% CI: {elo_lo:.0f} – {elo_hi:.0f})")

    # --- Confidence band ---
    x_curve = np.linspace(min(opp_elos) - 100, max(opp_elos) + 100, 300)
    y_curve = np.array([elo_expected(x, r_engine) for x in x_curve])

    boot_curves = np.zeros((len(boot_elos), len(x_curve)))
    for i, r_e in enumerate(boot_elos):
        boot_curves[i] = [elo_expected(x, r_e) for x in x_curve]
    band_lo = np.percentile(boot_curves, 2.5, axis=0)
    band_hi = np.percentile(boot_curves, 97.5, axis=0)

    # --- Plot ---
    # Project palette: green #7EC8A4, blue #A8D8EA, pink #F4A7A0,
    #   dark blue #1976D2, dark red #D32F2F, grays #999999/#333333
    fig, ax = plt.subplots(figsize=(12, 7))

    ax.fill_between(x_curve, band_lo, band_hi,
                    color="#A8D8EA", alpha=0.35, label="95% CI (model)")
    ax.plot(x_curve, y_curve, color="#1976D2", linewidth=2.5, label="Elo model fit")

    ax.errorbar(opp_elos, means,
                yerr=[means - ci_lo, ci_hi - means],
                fmt="o", color="#1976D2", markersize=8, capsize=5,
                capthick=1.5, elinewidth=1.5, zorder=5,
                label="Observed score rate (95% CI)")

    for i, elo in enumerate(opp_elos):
        pct = f"{means[i]:.0%}"
        ax.annotate(pct, (elo, means[i]),
                    textcoords="offset points", xytext=(0, 14),
                    ha="center", fontsize=10, fontweight="bold",
                    color="#333333")

    ax.axhline(0.5, color="#999999", linestyle="--", linewidth=1,
               label="50% score rate")
    ax.axvline(elo_median, color="#1976D2", linestyle="--", linewidth=1.2,
               alpha=0.7)

    elo_text = f"Estimated OmegaZero Elo\n{elo_median:.0f} ± {elo_half_ci}"
    bbox_props = dict(boxstyle="round,pad=0.5", facecolor="white",
                      edgecolor="#1976D2", linewidth=2)
    ax.annotate(elo_text,
                xy=(elo_median, 0.5), xytext=(elo_median - 50, 0.22),
                fontsize=14, fontweight="bold", color="#1976D2",
                ha="center", va="top", bbox=bbox_props,
                arrowprops=dict(arrowstyle="-", color="#1976D2",
                                linewidth=1.2))

    ax.set_xlabel("Stockfish Elo", fontsize=12)
    ax.set_ylabel("Score Rate [W + 0.5D]", fontsize=12)
    ax.set_title("Elo Estimation vs Stockfish",
                 fontsize=15, fontweight="bold", pad=15)
    ax.set_ylim(-0.02, 1.05)
    ax.legend(loc="upper right", fontsize=10)
    ax.grid(True, alpha=0.3, axis="y")

    fig.tight_layout()
    plot_path = Path(output_dir) / "elo.png"
    fig.savefig(plot_path, dpi=150)
    plt.close(fig)
    print(f"  Plot saved: {plot_path}")


def main():
    parser = argparse.ArgumentParser(
        description="OmegaZero Elo estimation via logistic curve fitting"
    )
    sub = parser.add_subparsers(dest="command")
    sub.required = True

    run_p = sub.add_parser("run", help="Run matches and estimate Elo")
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
        "--elo-levels", default="1700,1800,1900,2000,2100,2200,2300",
        help="Comma-separated opponent Elo levels",
    )
    run_p.add_argument(
        "--games", type=int, default=500,
        help="Games per level (default: 500)",
    )
    run_p.add_argument(
        "--st", default="1",
        help="Fixed time per move in seconds (default: 1)",
    )
    run_p.add_argument(
        "--concurrency", type=int, default=8,
        help="Number of concurrent games (default: 8)",
    )

    plot_p = sub.add_parser("plot", help="Regenerate plot from summary.csv")
    plot_p.add_argument(
        "summary_csv",
        help="Path to a summary.csv from a previous run",
    )

    args = parser.parse_args()

    if args.command == "run":
        run_matches(args)
    elif args.command == "plot":
        generate_plot(args.summary_csv)


if __name__ == "__main__":
    main()
