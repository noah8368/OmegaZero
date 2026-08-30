#!/usr/bin/env python3
"""
Quick Elo estimate for OmegaZero against a single set-Elo opponent.

Plays a fixed number of games (default 200) against one opponent whose rating is
known/assumed, then inverts the logistic Elo model to get a point estimate with a
95% confidence interval:

    Elo(OmegaZero) = Elo(opponent) - 400 * log10(1 / score - 1)

This is the fast "roughly where am I" tool. Precision is driven by total games and
by how close the opponent is to OmegaZero's strength: games against a ~50%-score
opponent carry the most information, so pick --opp-elo near OmegaZero's expected
rating. ~200 games near 50% gives roughly +-50 Elo (95%).

Opponent options:
    - Default: Stockfish with UCI_LimitStrength=true + UCI_Elo=<--opp-elo>. This is a
      "set Elo opponent" that runs locally, but its rating is on Stockfish's INTERNAL
      scale, not CCRL/FIDE. Good for relative tracking between OmegaZero versions.
    - Absolute: pass --opp-cmd <engine> --opp-elo <its true CCRL rating> (optionally
      --opp-proto xboard). A real fixed-strength engine is run at full strength, so the
      estimate lands on that engine's rating scale.

Results are saved to results/elo/<run_dir>/:
    summary.csv   — opponent_elo, games, wins, draws, losses, score_rate, elo, elo_lo, elo_hi.
    games.pgn     — raw PGN of every game played.

Usage:
    python3 scripts/elo.py run                          # 200 games vs SF @2000, 0.2s/move
    python3 scripts/elo.py run --opp-elo 2100           # aim the opponent near your strength
    python3 scripts/elo.py run --games 400 --st 0.5     # tighter estimate, slower
    python3 scripts/elo.py run --tc 8+0.08              # real clock (exercises dynamic TM)
    python3 scripts/elo.py run --opp-cmd bin/fruit --opp-proto uci --opp-elo 2456
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
DEFAULT_OPENINGS = REPO_ROOT / "openings.pgn"

EPSILON = 1e-9


def score_to_elo_diff(score):
    """Elo advantage implied by a score rate in (0, 1), via the logistic model."""
    score = min(1.0 - EPSILON, max(EPSILON, score))
    return -400.0 * math.log10(1.0 / score - 1.0)


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


def resolve_cutechess(arg):
    if arg is not None:
        return arg
    local_build = REPO_ROOT / "cutechess" / "build" / "cutechess-cli"
    if local_build.exists():
        return str(local_build)
    if shutil.which("cutechess-cli"):
        return "cutechess-cli"
    sys.exit(
        "cutechess-cli not found.\n"
        "Looked in: cutechess/build/cutechess-cli and PATH.\n"
        "Use --cutechess to specify the path."
    )


def build_opponent_clause(args):
    """Return (cutechess -engine args, human-readable opponent label)."""
    if args.opp_cmd:
        opp = Path(args.opp_cmd)
        if not opp.is_absolute():
            opp = REPO_ROOT / opp
        opp = opp.resolve()
        if not opp.exists():
            sys.exit(f"Opponent engine not found: {opp}")
        name = args.opp_name or f"{opp.stem}-{args.opp_elo}"
        # Full-strength fixed opponent; no strength limiting.
        return [
            "-engine", f"name={name}", f"cmd={opp}", f"proto={args.opp_proto}",
        ], name

    # Default: Stockfish limited to a set UCI_Elo (local "set Elo opponent").
    sf = args.stockfish
    if not Path(sf).exists() and not shutil.which(sf):
        sys.exit(f"Stockfish not found: {sf}\nInstall: brew install stockfish")
    name = args.opp_name or f"SF-{args.opp_elo}"
    return [
        "-engine", f"name={name}", f"cmd={sf}", "proto=uci",
            "option.UCI_LimitStrength=true",
            f"option.UCI_Elo={args.opp_elo}",
    ], name


def run_matches(args):
    engine = Path(args.engine)
    if not engine.is_absolute():
        engine = REPO_ROOT / engine
    engine = engine.resolve()
    if not engine.exists():
        sys.exit(f"Engine not found: {engine}\nRun 'make' first.")

    cutechess = resolve_cutechess(args.cutechess)
    opp_clause, opp_name = build_opponent_clause(args)

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    tag = get_version_tag()
    run_dir = RESULTS_BASE / f"{ts}_{tag}"
    run_dir.mkdir(parents=True, exist_ok=True)

    # Real clock (tc=) when --tc is given, otherwise fixed time per move (st=).
    if args.tc:
        tc_clause = f"tc={args.tc}"
        tc_desc = f"tc {args.tc}"
    else:
        tc_clause = f"st={args.st}"
        tc_desc = f"{args.st}s/move"

    # Color-balanced pairs: each opening is played twice with reversed colors, so
    # round it up to an even game count.
    rounds = (args.games + 1) // 2
    total_games = rounds * 2

    cmd = [
        cutechess,
        "-engine", "name=OmegaZero", f"cmd={engine}",
            "arg=--uci", "proto=uci",
        *opp_clause,
        "-each", tc_clause, "timemargin=500",
        "-rounds", str(rounds), "-games", "2", "-repeat",
        "-concurrency", str(args.concurrency),
        "-pgnout", str(run_dir / "games.pgn"),
        "-recover",
    ]

    # Vary the starting position so games are not near-duplicates.
    openings = Path(args.openings) if args.openings else DEFAULT_OPENINGS
    if openings.exists():
        cmd += ["-openings", f"file={openings}", "format=pgn", "order=random"]
    else:
        print(f"  (no openings file at {openings}; playing from the start position)",
              flush=True)

    print(f"\n{'=' * 60}", flush=True)
    print(f"  OmegaZero vs {opp_name}  |  {total_games} games  |  {tc_desc}",
          flush=True)
    print(f"{'=' * 60}", flush=True)

    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )

    wins, draws, losses = 0, 0, 0
    for line in proc.stdout:
        line = line.strip()
        m = re.match(r"Finished game (\d+) \((.+?) vs (.+?)\): (\S+)", line)
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
        print(f"  {total:3d}/{total_games}  {tag_str}  "
              f"W:{wins} D:{draws} L:{losses}  Score: {rate:.3f}", flush=True)

    proc.wait()
    proc.stderr.read()

    total = wins + draws + losses
    if total == 0:
        sys.exit("No games completed — check the engine/opponent commands.")

    result = estimate_elo(wins, draws, losses, args.opp_elo)
    write_summary(run_dir, args.opp_elo, wins, draws, losses, result)
    print_result(opp_name, args.opp_elo, wins, draws, losses, result)
    print(f"\nResults saved to {run_dir}/")


def estimate_elo(wins, draws, losses, opp_elo):
    """Point estimate + 95% CI for OmegaZero's Elo from a W/D/L record."""
    n = wins + draws + losses
    score = (wins + 0.5 * draws) / n

    # Standard error of the mean score, accounting for the three outcome values.
    var = (wins * (1.0 - score) ** 2 +
           draws * (0.5 - score) ** 2 +
           losses * (0.0 - score) ** 2) / max(n - 1, 1)
    se = math.sqrt(var / n)

    score_lo = max(0.0, score - 1.96 * se)
    score_hi = min(1.0, score + 1.96 * se)

    elo = opp_elo + score_to_elo_diff(score)
    elo_lo = opp_elo + score_to_elo_diff(score_lo)
    elo_hi = opp_elo + score_to_elo_diff(score_hi)
    return {
        "score_rate": round(score, 4),
        "elo": round(elo),
        "elo_lo": round(elo_lo),
        "elo_hi": round(elo_hi),
    }


def write_summary(run_dir, opp_elo, wins, draws, losses, result):
    summary_csv = run_dir / "summary.csv"
    with open(summary_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=[
            "opponent_elo", "games", "wins", "draws", "losses",
            "score_rate", "elo", "elo_lo", "elo_hi",
        ])
        w.writeheader()
        w.writerow({
            "opponent_elo": opp_elo,
            "games": wins + draws + losses,
            "wins": wins,
            "draws": draws,
            "losses": losses,
            "score_rate": result["score_rate"],
            "elo": result["elo"],
            "elo_lo": result["elo_lo"],
            "elo_hi": result["elo_hi"],
        })


def print_result(opp_name, opp_elo, wins, draws, losses, result):
    n = wins + draws + losses
    half = round((result["elo_hi"] - result["elo_lo"]) / 2)
    print(f"\n{'=' * 60}")
    print(f"  Opponent : {opp_name}  (Elo {opp_elo})")
    print(f"  Record   : {wins}W  {draws}D  {losses}L   ({n} games)")
    print(f"  Score    : {result['score_rate']:.4f}")
    print(f"\n  Estimated OmegaZero Elo: {result['elo']} +- {half} "
          f"(95% CI: {result['elo_lo']} - {result['elo_hi']})")
    print(f"{'=' * 60}")


def main():
    parser = argparse.ArgumentParser(
        description="Quick OmegaZero Elo estimate vs a single set-Elo opponent"
    )
    sub = parser.add_subparsers(dest="command")
    sub.required = True

    run_p = sub.add_parser("run", help="Play matches and estimate Elo")
    run_p.add_argument(
        "--engine", default="build/OmegaZero",
        help="Path to OmegaZero binary (default: build/OmegaZero)",
    )
    run_p.add_argument(
        "--opp-elo", type=int, default=2000,
        help="Opponent's set Elo — used as the SF UCI_Elo AND as the anchor "
             "rating in the estimate. Pick it near OmegaZero's expected strength "
             "for the tightest result (default: 2000).",
    )
    run_p.add_argument(
        "--games", type=int, default=200,
        help="Total games to play (rounded up to even; default: 200)",
    )
    run_p.add_argument(
        "--stockfish", default="stockfish",
        help="Path or command for Stockfish, the default opponent "
             "(default: stockfish)",
    )
    run_p.add_argument(
        "--opp-cmd", default=None,
        help="Use a custom fixed-strength engine as the opponent instead of "
             "limited Stockfish. Run at full strength; pass its true rating via "
             "--opp-elo for an absolute (on that engine's scale) estimate.",
    )
    run_p.add_argument(
        "--opp-proto", default="uci", choices=["uci", "xboard"],
        help="Protocol for --opp-cmd (default: uci)",
    )
    run_p.add_argument(
        "--opp-name", default=None,
        help="Display/PGN name for the opponent (default: auto)",
    )
    run_p.add_argument(
        "--cutechess", default=None,
        help="Path to cutechess-cli (default: auto-detect)",
    )
    run_p.add_argument(
        "--openings", default=None,
        help=f"PGN openings file for varied start positions "
             f"(default: {DEFAULT_OPENINGS.name} if present)",
    )
    run_p.add_argument(
        "--st", default="0.2",
        help="Fixed time per move in seconds (default: 0.2). Ignored if --tc set.",
    )
    run_p.add_argument(
        "--tc", default=None,
        help="Cutechess time control (e.g. '8+0.08'). Overrides --st and drives "
             "the engine with a real clock, exercising dynamic time management.",
    )
    run_p.add_argument(
        "--concurrency", type=int, default=8,
        help="Number of concurrent games (default: 8)",
    )

    args = parser.parse_args()
    if args.command == "run":
        run_matches(args)


if __name__ == "__main__":
    main()
