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

For a firm rating on the CCRL scale (rather than Stockfish's internal set-Elo
scale), use the `calibrate` subcommand: it plays a round-robin over OmegaZero and
a set of fixed-rating anchor engines (built by scripts/fetch_anchors.sh, listed in
scripts/anchors.json) and fits the result with Ordo, holding the anchors fixed.
Anchor-vs-anchor games plus a floated diagnostic pass flag any stale anchor.

    python3 scripts/elo.py calibrate                    # all built anchors, 100 games/pairing
    python3 scripts/elo.py calibrate --anchors blunder-7.2.0,fruit-2.1,blunder-7.6.0
"""

import argparse
import csv
import json
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
DEFAULT_ANCHORS = SCRIPT_DIR / "anchors.json"
DEFAULT_ORDO = REPO_ROOT / "engines" / "ordo" / "ordo"

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
        # Pin OmegaZero to 1 thread: it otherwise defaults Threads to the core
        # count (Lazy SMP), which oversubscribes cores under --concurrency and
        # corrupts clock-based timing. The opponent (Stockfish/Fruit) defaults to
        # a single thread already.
        "-engine", "name=OmegaZero", f"cmd={engine}",
            "arg=--uci", "proto=uci", f"option.Threads={args.threads}",
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


# ===========================================================================
# calibrate — CCRL-scale rating via a round-robin over OmegaZero + fixed anchors,
# fitted with Ordo. Unlike `run` (one set-Elo opponent, logistic inversion), this
# plays an all-play-all — including anchor-vs-anchor games, which let the fit and
# the residual check catch a stale/mis-transferred anchor instead of trusting it.
# ===========================================================================

def load_anchors(anchors_file, names):
    """Load the anchor registry, select a subset, and resolve built binaries."""
    try:
        data = json.loads(Path(anchors_file).read_text())
    except FileNotFoundError:
        sys.exit(f"Anchor registry not found: {anchors_file}")
    all_anchors = data.get("anchors", [])
    if names:
        wanted = [n.strip() for n in names.split(",") if n.strip()]
        by_name = {a["name"]: a for a in all_anchors}
        missing = [n for n in wanted if n not in by_name]
        if missing:
            sys.exit(f"Unknown anchors (not in {anchors_file}): {', '.join(missing)}")
        chosen = [by_name[n] for n in wanted]
    else:
        chosen = all_anchors

    resolved = []
    for a in chosen:
        path = Path(a["cmd"])
        if not path.is_absolute():
            path = REPO_ROOT / path
        if not path.exists():
            print(f"  (skip anchor {a['name']}: binary not built at {path} — "
                  f"run scripts/fetch_anchors.sh)", flush=True)
            continue
        a = dict(a)
        a["path"] = str(path.resolve())
        resolved.append(a)

    if len(resolved) < 2:
        sys.exit("Need >=2 anchors with built binaries for a fixed-anchor fit.\n"
                 "Build them with scripts/fetch_anchors.sh.")
    return resolved


def parse_ordo_csv(path):
    """Parse an Ordo -c CSV into {name: {rating, error, points, played}}."""
    out = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            err = row["ERROR"].strip()
            out[row["PLAYER"]] = {
                "rating": float(row["RATING"]),
                # Anchors held fixed report "-" (no error bar).
                "error": None if err in ("-", "----", "") else float(err),
                "points": float(row["POINTS"]),
                "played": int(float(row["PLAYED"])),
            }
    return out


class OrdoError(RuntimeError):
    """Ordo failed or did not converge; the caller decides if it's fatal."""


def run_ordo(ordo, pgn, out_csv, anchors_file=None, avg=None, sims=1000,
             cpus=None, timeout=180):
    """Run one Ordo fit and return parsed ratings.

    Ordo aborts if the game graph is not fully connected (few games or anchors
    far from OmegaZero, producing all-win/all-loss players); retry once with -G
    (force) and a loud warning rather than crash. An unconstrained fit (only the
    average pinned) can also fail to converge on such degenerate data and spin
    forever, so every call is bounded by `timeout`. Raises OrdoError on failure.
    """
    base = [str(ordo), "-Q", "-p", str(pgn), "-c", str(out_csv),
            "-s", str(sims), "-W", "-D"]
    if anchors_file is not None:
        base += ["-m", str(anchors_file)]       # hold listed anchors fixed
    if avg is not None:
        base += ["-a", str(avg)]                # pin the pool average instead
    if cpus:
        base += ["-n", str(cpus)]

    def _run(cmd):
        try:
            return subprocess.run(cmd, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.PIPE, timeout=timeout)
        except subprocess.TimeoutExpired:
            raise OrdoError(f"Ordo did not finish within {timeout}s "
                            "(likely a degenerate/poorly-connected game graph).")

    proc = _run(base)
    if proc.returncode != 0:
        print("  WARNING: Ordo game graph poorly connected — retrying with "
              "--force.\n  Ratings are approximate; use more --games or a closer "
              "anchor bracket.", flush=True)
        proc = _run(base + ["-G"])
        if proc.returncode != 0:
            raise OrdoError("Ordo failed:\n" + proc.stderr.decode(errors="replace"))
    return parse_ordo_csv(out_csv)


def run_calibrate(args):
    engine = Path(args.engine)
    if not engine.is_absolute():
        engine = REPO_ROOT / engine
    engine = engine.resolve()
    if not engine.exists():
        sys.exit(f"Engine not found: {engine}\nRun 'make' first.")

    ordo = Path(args.ordo)
    if not ordo.is_absolute():
        ordo = REPO_ROOT / ordo
    if not ordo.exists():
        sys.exit(f"Ordo not found: {ordo}\nBuild it with scripts/fetch_anchors.sh.")

    cutechess = resolve_cutechess(args.cutechess)
    anchors = load_anchors(args.anchors_file, args.anchors)

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    tag = get_version_tag()
    run_dir = RESULTS_BASE / f"{ts}_{tag}_calibrate"
    run_dir.mkdir(parents=True, exist_ok=True)
    pgn_path = run_dir / "games.pgn"

    # Calibration is always clocked (a real TC), never fixed movetime.
    tc_clause = f"tc={args.tc}"

    # Color-balanced pairs, rounds per pairing.
    rounds = max(1, (args.games + 1) // 2)
    n_engines = 1 + len(anchors)
    pairs = n_engines * (n_engines - 1) // 2
    total_games = pairs * rounds * 2

    # OmegaZero pinned to 1 thread + ponder off to match CCRL conditions. Anchor
    # engines get only their registry-declared options (never a blanket
    # option.Threads, which cutechess rejects on engines that lack it).
    oz_clause = [
        "-engine", "name=OmegaZero", f"cmd={engine}", "arg=--uci", "proto=uci",
        f"option.Threads={args.threads}",
    ]
    cmd = [cutechess, *oz_clause]
    for a in anchors:
        clause = ["-engine", f"name={a['name']}", f"cmd={a['path']}",
                  f"proto={a.get('proto', 'uci')}"]
        for opt, val in (a.get("options") or {}).items():
            clause.append(f"option.{opt}={val}")
        cmd += clause
    cmd += [
        "-each", tc_clause, "timemargin=500",
        "-tournament", "round-robin",
        "-rounds", str(rounds), "-games", "2", "-repeat",
        "-concurrency", str(args.concurrency),
        "-pgnout", str(pgn_path),
        "-ratinginterval", "0",
        "-recover",
    ]
    openings = Path(args.openings) if args.openings else DEFAULT_OPENINGS
    if openings.exists():
        cmd += ["-openings", f"file={openings}", "format=pgn", "order=random"]
    else:
        print(f"  (no openings file at {openings}; playing from the start position)",
              flush=True)

    print(f"\n{'=' * 68}", flush=True)
    print(f"  CCRL calibration round-robin", flush=True)
    print(f"  {n_engines} engines ({len(anchors)} anchors), {pairs} pairings, "
          f"{total_games} games @ {args.tc}", flush=True)
    print(f"  anchors: {', '.join(a['name'] for a in anchors)}", flush=True)
    print(f"{'=' * 68}", flush=True)

    # Head-to-head tally of OmegaZero vs each anchor (for the curve plot), plus a
    # completed-game counter for progress.
    oz_vs = {a["name"]: [0, 0, 0] for a in anchors}   # [w, d, l] from OZ's side
    done = 0
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True)
    for line in proc.stdout:
        m = re.match(r"Finished game (\d+) \((.+?) vs (.+?)\): (\S+)", line.strip())
        if not m:
            continue
        done += 1
        white, black, result = m.group(2), m.group(3), m.group(4)
        if "OmegaZero" in (white, black):
            opp = black if white == "OmegaZero" else white
            oz_white = white == "OmegaZero"
            if result == "1-0":
                s = 1.0 if oz_white else 0.0
            elif result == "0-1":
                s = 0.0 if oz_white else 1.0
            else:
                s = 0.5
            if opp in oz_vs:
                oz_vs[opp][0 if s == 1.0 else (1 if s == 0.5 else 2)] += 1
        if done % 10 == 0 or done == total_games:
            print(f"  {done:5d}/{total_games} games", flush=True)
    proc.wait()
    proc.stderr.read()

    if not pgn_path.exists() or pgn_path.stat().st_size == 0:
        sys.exit("No games recorded — check the engine/anchor commands.")

    # --- Ordo fit (anchors held fixed) -> OmegaZero on CCRL scale ---------------
    anchors_txt = run_dir / "ordo_anchors.txt"
    with open(anchors_txt, "w") as f:
        for a in anchors:
            f.write(f'"{a["name"]}",{a["elo"]}\n')

    cpus = args.concurrency
    try:
        fixed = run_ordo(ordo, pgn_path, run_dir / "ratings.csv",
                         anchors_file=anchors_txt, sims=args.sims, cpus=cpus)
    except OrdoError as e:
        sys.exit(f"Primary Ordo fit failed: {e}")
    if "OmegaZero" not in fixed:
        sys.exit("Ordo did not rate OmegaZero — too few connected games?")

    # --- Diagnostic fit (all float, pool average pinned) -> anchor residuals ----
    # Non-fatal: an unconstrained fit can diverge on degenerate data. If it fails,
    # report OmegaZero's rating without the residual column rather than abort.
    avg = sum(a["elo"] for a in anchors) / len(anchors)
    try:
        floated = run_ordo(ordo, pgn_path, run_dir / "ratings_floated.csv",
                           avg=round(avg), sims=args.sims, cpus=cpus)
    except OrdoError as e:
        print(f"  (skipping anchor-residual diagnostic: {e})", flush=True)
        floated = {}

    result = summarize_calibration(run_dir, anchors, fixed, floated, oz_vs, args)
    print_calibration(anchors, fixed, floated, oz_vs, result)
    plot_calibration(run_dir, anchors, fixed, oz_vs)
    print(f"\nResults saved to {run_dir}/")


def summarize_calibration(run_dir, anchors, fixed, floated, oz_vs, args):
    """Write summary.csv and return OmegaZero's rating + CI."""
    oz = fixed["OmegaZero"]
    err = oz["error"]
    result = {
        "elo": round(oz["rating"]),
        "elo_lo": round(oz["rating"] - err) if err is not None else None,
        "elo_hi": round(oz["rating"] + err) if err is not None else None,
        "error": round(err) if err is not None else None,
    }
    with open(run_dir / "summary.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["engine", "role", "published_elo", "fitted_elo",
                    "error", "floated_elo", "residual", "vs_oz_w_d_l"])
        w.writerow(["OmegaZero", "test", "", round(oz["rating"]),
                    result["error"], round(floated.get("OmegaZero", oz)["rating"]),
                    "", ""])
        for a in anchors:
            name = a["name"]
            fl = floated.get(name, {}).get("rating")
            residual = round(fl - a["elo"]) if fl is not None else ""
            role = "independent" if a.get("independent") else "anchor"
            wdl = "/".join(str(x) for x in oz_vs.get(name, [0, 0, 0]))
            fx = fixed.get(name)
            fitted = round(fx["rating"]) if fx else ""
            w.writerow([name, role, a["elo"], fitted,
                        "", round(fl) if fl is not None else "", residual, wdl])
    return result


def print_calibration(anchors, fixed, floated, oz_vs, result):
    print(f"\n{'=' * 68}")
    if result["error"] is not None:
        print(f"  OmegaZero (CCRL scale): {result['elo']} +- {result['error']}  "
              f"(95% CI {result['elo_lo']} - {result['elo_hi']})")
    else:
        print(f"  OmegaZero (CCRL scale): {result['elo']}")
    print(f"{'-' * 68}")
    print(f"  Anchor residuals (floated - published; large => suspect anchor):")
    print(f"  {'anchor':<16} {'pub':>6} {'float':>6} {'resid':>6}  {'OZ W/D/L':>10}")
    for a in anchors:
        name = a["name"]
        fl = floated.get(name, {}).get("rating")
        resid = (fl - a["elo"]) if fl is not None else None
        flag = "  <-- check" if (resid is not None and abs(resid) > 40) else ""
        wdl = "/".join(str(x) for x in oz_vs.get(name, [0, 0, 0]))
        mark = " *" if a.get("independent") else "  "
        print(f" {mark}{name:<16} {a['elo']:>6} "
              f"{round(fl) if fl is not None else '?':>6} "
              f"{round(resid) if resid is not None else '?':>+6}  {wdl:>10}{flag}")
    print(f"  (* = independent cross-check anchor)")
    print(f"{'=' * 68}")


def plot_calibration(run_dir, anchors, fixed, oz_vs):
    """OmegaZero's Elo curve fit on the CCRL scale.

    Same style as elo.py's `run` plot (score rate vs opponent Elo, with a fitted
    logistic + model CI band), but the x-axis is the anchors' fixed CCRL ratings
    and the curve is centred on OmegaZero's Ordo-fitted rating (CI band from its
    error), rather than a per-opponent logistic fit. The independent cross-check
    anchor is drawn distinctly.
    """
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("  (matplotlib/numpy not installed; skipping calibration plot)",
              flush=True)
        return

    oz = fixed["OmegaZero"]
    oz_elo = oz["rating"]
    oz_err = oz["error"]

    def expected(opp_elo, r):
        # OmegaZero (rating r) expected score vs an opponent rated opp_elo.
        return 1.0 / (1.0 + 10 ** ((opp_elo - r) / 400.0))

    # Observed OmegaZero-vs-anchor points, split by anchor family.
    def collect(independent):
        xs, means, ci_lo, ci_hi, names = [], [], [], [], []
        for a in anchors:
            if bool(a.get("independent")) != independent:
                continue
            w, d, l = oz_vs.get(a["name"], [0, 0, 0])
            n = w + d + l
            if n == 0:
                continue
            mu = (w + 0.5 * d) / n
            var = (w * (1.0 - mu) ** 2 + d * (0.5 - mu) ** 2 +
                   l * (0.0 - mu) ** 2) / max(n - 1, 1)
            se = math.sqrt(var / n)
            xs.append(a["elo"])
            means.append(mu)
            ci_lo.append(max(0.0, mu - 1.96 * se))
            ci_hi.append(min(1.0, mu + 1.96 * se))
            names.append(a["name"])
        return xs, means, ci_lo, ci_hi, names

    anchor_elos = [a["elo"] for a in anchors]
    x_curve = np.linspace(min(anchor_elos) - 100, max(anchor_elos) + 100, 300)
    y_curve = np.array([expected(x, oz_elo) for x in x_curve])

    # Project palette: dark blue #1976D2, light blue #A8D8EA, dark red #D32F2F,
    #   grays #999999/#333333.
    fig, ax = plt.subplots(figsize=(12, 7))

    if oz_err is not None:
        # Curve uncertainty from OmegaZero's rating error: shift the centre +-err.
        band_hi = np.array([expected(x, oz_elo + oz_err) for x in x_curve])
        band_lo = np.array([expected(x, oz_elo - oz_err) for x in x_curve])
        ax.fill_between(x_curve, band_lo, band_hi,
                        color="#A8D8EA", alpha=0.35, label="95% CI (model)")
    ax.plot(x_curve, y_curve, color="#1976D2", linewidth=2.5,
            label="Elo model fit")

    for independent, color, lbl in (
        (False, "#1976D2", "Blunder anchor (95% CI)"),
        (True, "#D32F2F", "Independent anchor (95% CI)"),
    ):
        xs, means, ci_lo, ci_hi, names = collect(independent)
        if not xs:
            continue
        means_a = np.array(means)
        ax.errorbar(xs, means_a,
                    yerr=[means_a - np.array(ci_lo), np.array(ci_hi) - means_a],
                    fmt="D" if independent else "o", color=color, markersize=8,
                    capsize=5, capthick=1.5, elinewidth=1.5, zorder=5, label=lbl)
        for x, mu in zip(xs, means_a):
            ax.annotate(f"{mu:.0%}", (x, mu), textcoords="offset points",
                        xytext=(0, 14), ha="center", fontsize=10,
                        fontweight="bold", color="#333333")

    ax.axhline(0.5, color="#999999", linestyle="--", linewidth=1,
               label="50% score rate")
    ax.axvline(oz_elo, color="#1976D2", linestyle="--", linewidth=1.2, alpha=0.7)

    if oz_err is not None:
        elo_text = f"Estimated OmegaZero Elo\n{oz_elo:.0f} ± {round(oz_err)}"
    else:
        elo_text = f"Estimated OmegaZero Elo\n{oz_elo:.0f}"
    bbox_props = dict(boxstyle="round,pad=0.5", facecolor="white",
                      edgecolor="#1976D2", linewidth=2)
    ax.annotate(elo_text, xy=(oz_elo, 0.5), xytext=(oz_elo, 0.22),
                fontsize=14, fontweight="bold", color="#1976D2",
                ha="center", va="top", bbox=bbox_props,
                arrowprops=dict(arrowstyle="-", color="#1976D2", linewidth=1.2))

    ax.set_xlabel("Anchor Elo (CCRL)", fontsize=12)
    ax.set_ylabel("Score Rate [W + 0.5D]", fontsize=12)
    ax.set_title("CCRL-Anchored Elo Estimation",
                 fontsize=15, fontweight="bold", pad=15)
    ax.set_ylim(-0.02, 1.05)
    ax.legend(loc="upper right", fontsize=10)
    ax.grid(True, alpha=0.3, axis="y")

    fig.tight_layout()
    fig.savefig(run_dir / "calibration.png", dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="OmegaZero Elo: a quick estimate vs a single set-Elo opponent "
                    "(run), or a firm CCRL-scale rating from an anchored "
                    "round-robin (calibrate)."
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
    run_p.add_argument(
        "--threads", type=int, default=1,
        help="OmegaZero Threads option (default: 1). Keep at 1 and scale games "
             "via --concurrency; >1 oversubscribes cores and biases timing.",
    )

    cal_p = sub.add_parser(
        "calibrate",
        help="CCRL-scale rating via a round-robin over OmegaZero + fixed anchors, "
             "fitted with Ordo",
    )
    cal_p.add_argument(
        "--engine", default="build/OmegaZero",
        help="Path to OmegaZero binary (default: build/OmegaZero)",
    )
    cal_p.add_argument(
        "--anchors", default=None,
        help="Comma-separated anchor names to include (default: all in the "
             "registry that have a built binary). Pick a bracket spanning "
             "~+-200 Elo around OmegaZero for the tightest fit.",
    )
    cal_p.add_argument(
        "--anchors-file", default=str(DEFAULT_ANCHORS),
        help=f"Anchor registry JSON (default: {DEFAULT_ANCHORS.name})",
    )
    cal_p.add_argument(
        "--games", type=int, default=100,
        help="Games per pairing (rounded up to even; default: 100). Total games "
             "= pairings * this, and pairings grow as O(anchors^2).",
    )
    cal_p.add_argument(
        "--tc", default="10+0.1",
        help="Cutechess time control, shared by all engines (default: 10+0.1). "
             "Match CCRL conditions; calibration is always clocked.",
    )
    cal_p.add_argument(
        "--threads", type=int, default=1,
        help="OmegaZero Threads option (default: 1, to match CCRL conditions)",
    )
    cal_p.add_argument(
        "--sims", type=int, default=1000,
        help="Ordo bootstrap simulations for the CI (default: 1000)",
    )
    cal_p.add_argument(
        "--ordo", default=str(DEFAULT_ORDO),
        help=f"Path to the Ordo binary (default: {DEFAULT_ORDO})",
    )
    cal_p.add_argument(
        "--openings", default=None,
        help=f"PGN openings file (default: {DEFAULT_OPENINGS.name} if present)",
    )
    cal_p.add_argument(
        "--cutechess", default=None,
        help="Path to cutechess-cli (default: auto-detect)",
    )
    cal_p.add_argument(
        "--concurrency", type=int, default=8,
        help="Concurrent games, also Ordo simulation CPUs (default: 8)",
    )

    args = parser.parse_args()
    if args.command == "run":
        run_matches(args)
    elif args.command == "calibrate":
        run_calibrate(args)


if __name__ == "__main__":
    main()
