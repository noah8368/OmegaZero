#!/usr/bin/env python3
"""
OmegaZero SPRT testing.

Subcommands:
    run       — Run a single SPRT test between two engine versions.
    gauntlet  — Run SPRT across all consecutive version tags (v1→v2→...→vN).
    plot      — Regenerate plots from sprt_history.csv.
    history   — Show SPRT test history.

Usage:
    # Single test against a commit
    python3 scripts/sprt.py run --baseline-commit HEAD~1

    # Single test against a binary
    python3 scripts/sprt.py run --baseline build/OmegaZero_base

    # Run the full version gauntlet with SPRT
    python3 scripts/sprt.py gauntlet

    # Gauntlet with custom settings
    python3 scripts/sprt.py gauntlet --st 0.5 --concurrency 4

    # Resume an interrupted gauntlet
    python3 scripts/sprt.py gauntlet --resume

    # Regenerate plots from existing results
    python3 scripts/sprt.py plot

    # Show past results
    python3 scripts/sprt.py history
"""

import argparse
import csv
import math
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
RESULTS_DIR = REPO_ROOT / "results" / "sprt"
HISTORY_CSV = RESULTS_DIR / "sprt_history.csv"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL, cwd=REPO_ROOT,
        ).decode().strip()
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
    """Find all tags matching v<number>, sorted numerically."""
    try:
        raw = subprocess.check_output(
            ["git", "tag", "-l", "v*"],
            stderr=subprocess.DEVNULL, cwd=REPO_ROOT,
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []

    tags = []
    for line in raw.splitlines():
        tag = line.strip()
        suffix = tag[1:]
        if suffix.isdigit():
            tags.append((int(suffix), tag))

    tags.sort()
    return [tag for _, tag in tags]


def build_at_commit(commit_ref, label):
    commit_hash = get_commit_short(commit_ref)
    print(f"  Building {label} ({commit_hash})...", flush=True)

    tmpdir = tempfile.mkdtemp(prefix=f"omega_sprt_{label}_")
    worktree = Path(tmpdir) / "worktree"

    try:
        subprocess.check_call(
            ["git", "worktree", "add", "--detach", str(worktree), commit_ref],
            cwd=REPO_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        shutil.rmtree(tmpdir, ignore_errors=True)
        sys.exit(f"Failed to create git worktree at {commit_ref}")

    for d in ["build", "build/play", "build/debug", "build/bench"]:
        (worktree / d).mkdir(parents=True, exist_ok=True)

    makefile = worktree / "Makefile"
    if makefile.exists():
        txt = makefile.read_text()
        makefile.write_text(txt.replace("-Werror", ""))

    try:
        subprocess.check_call(
            ["make", "-j", str(os.cpu_count() or 4)],
            cwd=worktree, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as e:
        cleanup_worktree(worktree)
        shutil.rmtree(tmpdir, ignore_errors=True)
        sys.exit(f"Build failed at {commit_hash}:\n{e.stderr.decode() if e.stderr else ''}")

    binary = worktree / "build" / "OmegaZero"
    if not binary.exists():
        cleanup_worktree(worktree)
        shutil.rmtree(tmpdir, ignore_errors=True)
        sys.exit(f"Binary not found after build at {commit_hash}")

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


def find_cutechess(cutechess_arg=None):
    if cutechess_arg:
        if Path(cutechess_arg).exists() or shutil.which(cutechess_arg):
            return cutechess_arg
        sys.exit(f"cutechess-cli not found: {cutechess_arg}")

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


# ---------------------------------------------------------------------------
# Core match runner (shared by run + gauntlet)
# ---------------------------------------------------------------------------

def run_match(cutechess, test_binary, test_label, base_binary, base_label,
              st, elo0, elo1, alpha, beta, max_games, concurrency,
              openings=None, pgn_path=None):
    """Run a single SPRT match. Returns a result dict or None on interrupt."""

    cmd = [
        cutechess,
        "-engine", f"name={test_label}", f"cmd={test_binary}",
            "arg=--uci", "proto=uci",
        "-engine", f"name={base_label}", f"cmd={base_binary}",
            "arg=--uci", "proto=uci",
        "-each", f"st={st}", "timemargin=500",
        "-sprt", f"elo0={elo0}", f"elo1={elo1}",
            f"alpha={alpha}", f"beta={beta}",
        "-rounds", str(max_games),
        "-games", "2",
        "-repeat",
        "-recover",
        "-ratinginterval", "10",
    ]

    if pgn_path:
        cmd.extend(["-pgnout", str(pgn_path)])

    if concurrency > 1:
        cmd.extend(["-concurrency", str(concurrency)])

    if openings:
        openings = Path(openings)
        if not openings.is_absolute():
            openings = REPO_ROOT / openings
        if not openings.exists():
            sys.exit(f"Openings file not found: {openings}")
        fmt = "epd" if str(openings).endswith(".epd") else "pgn"
        cmd.extend(["-openings", f"file={openings}", f"format={fmt}",
                     "order=random"])

    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )

    wins, draws, losses = 0, 0, 0
    games = []
    result_str = "INCONCLUSIVE"
    llr_final = 0.0

    try:
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue

            m = re.match(
                r"Finished game (\d+) \((.+?) vs (.+?)\): (.+)", line
            )
            if m:
                game_num = int(m.group(1))
                white = m.group(2)
                result = m.group(4).split("{")[0].strip()
                new_is_white = test_label in white

                if result == "1-0":
                    score = 1.0 if new_is_white else 0.0
                elif result == "0-1":
                    score = 0.0 if new_is_white else 1.0
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
                elo = elo_diff(rate)

                games.append({
                    "game": game_num,
                    "white": white,
                    "result": result,
                    "score": score,
                    "running_wins": wins,
                    "running_draws": draws,
                    "running_losses": losses,
                    "running_elo": round(elo, 1),
                })

                tag = "W" if score == 1.0 else ("D" if score == 0.5 else "L")
                print(f"    {total:5d}  {tag}  "
                      f"+{wins} ={draws} -{losses}  "
                      f"ELO: {elo:+.1f}", flush=True)

            if "SPRT" in line:
                print(f"\n    >> {line}", flush=True)
                if "H1 accepted" in line or "accepted H1" in line.lower():
                    result_str = "H1_ACCEPTED"
                elif "H0 accepted" in line or "accepted H0" in line.lower():
                    result_str = "H0_ACCEPTED"

            if "Elo" in line and ("LLR" in line or "difference" in line):
                print(f"    >> {line}", flush=True)
                llr_match = re.search(r"LLR:\s*([-\d.]+)", line)
                if llr_match:
                    llr_final = float(llr_match.group(1))

    except KeyboardInterrupt:
        print("\n  Interrupted!", flush=True)
        proc.terminate()
        proc.wait()
        return None, []

    proc.wait()

    stderr_out = proc.stderr.read()
    if stderr_out.strip():
        for sline in stderr_out.strip().splitlines():
            if "SPRT" in sline or "Elo" in sline or "LLR" in sline:
                print(f"    >> {sline}", flush=True)
                if "H1 accepted" in sline or "accepted H1" in sline.lower():
                    result_str = "H1_ACCEPTED"
                elif "H0 accepted" in sline or "accepted H0" in sline.lower():
                    result_str = "H0_ACCEPTED"

    total = wins + draws + losses
    if total == 0:
        return None, []

    rate = (wins + 0.5 * draws) / total
    elo = elo_diff(rate)

    summary = {
        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "test_label": test_label,
        "base_label": base_label,
        "test_commit": "",
        "base_commit": "",
        "elo0": elo0,
        "elo1": elo1,
        "alpha": alpha,
        "beta": beta,
        "st": st,
        "games": total,
        "wins": wins,
        "draws": draws,
        "losses": losses,
        "score_rate": round(rate, 4),
        "elo_diff": round(elo, 1),
        "llr": round(llr_final, 3),
        "result": result_str,
    }

    return summary, games


# ---------------------------------------------------------------------------
# History
# ---------------------------------------------------------------------------

HISTORY_FIELDS = [
    "timestamp", "test_label", "base_label", "test_commit", "base_commit",
    "elo0", "elo1", "alpha", "beta", "st",
    "games", "wins", "draws", "losses", "score_rate", "elo_diff",
    "llr", "result",
]


def append_to_history(summary):
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    file_exists = HISTORY_CSV.exists()
    with open(HISTORY_CSV, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=HISTORY_FIELDS)
        if not file_exists:
            w.writeheader()
        w.writerow(summary)


def load_history():
    if not HISTORY_CSV.exists():
        return []
    with open(HISTORY_CSV) as f:
        return list(csv.DictReader(f))


def completed_matchups():
    rows = load_history()
    return {(r["base_commit"], r["test_commit"]) for r in rows}


def show_history():
    rows = load_history()
    if not rows:
        print("No SPRT history found.")
        return

    print(f"\n{'=' * 90}")
    print("  SPRT History")
    print(f"{'=' * 90}")
    print(f"  {'Date':<20} {'Test':<18} {'Base':<18} "
          f"{'Games':>6} {'W-D-L':<14} {'ELO':>7} {'Result':<14}")
    print(f"  {'-'*20} {'-'*18} {'-'*18} "
          f"{'-'*6:>6} {'-'*14} {'-'*7:>7} {'-'*14}")

    for r in rows:
        wdl = f"+{r['wins']}={r['draws']}-{r['losses']}"
        res = r["result"]
        if res == "H1_ACCEPTED":
            tag = "PASSED"
        elif res == "H0_ACCEPTED":
            tag = "FAILED"
        elif res == "INTERRUPTED":
            tag = "INTERRUPTED"
        else:
            tag = "INCONCLUSIVE"

        print(f"  {r['timestamp']:<20} {r['test_label']:<18} "
              f"{r['base_label']:<18} {r['games']:>6} {wdl:<14} "
              f"{float(r['elo_diff']):>+7.1f} {tag:<14}")

    print(f"{'=' * 90}")


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

def generate_plots():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("\nmatplotlib not installed — skipping plots.")
        print("Install with: pip3 install matplotlib")
        return

    rows = load_history()
    if not rows:
        print("No history data for plots.")
        return

    # Use gauntlet rows (test_label contains a version tag like "v2-...")
    # Group by (base_label, test_label) and take the latest run for each pair
    latest = {}
    for r in rows:
        key = (r["base_label"], r["test_label"])
        latest[key] = r

    # Filter to version-tagged gauntlet matchups and sort by version number
    gauntlet_rows = []
    for (base_label, test_label), r in latest.items():
        m = re.match(r"^v(\d+)", test_label)
        if m:
            gauntlet_rows.append((int(m.group(1)), r))

    gauntlet_rows.sort()

    if not gauntlet_rows:
        print("No gauntlet results found for plotting.")
        return

    print("\nGenerating plots...")
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    labels = []
    wins_list = []
    draws_list = []
    losses_list = []
    elo_diffs = []

    for _, r in gauntlet_rows:
        labels.append(f"{r['test_label']}\nvs {r['base_label']}")
        wins_list.append(int(r["wins"]))
        draws_list.append(int(r["draws"]))
        losses_list.append(int(r["losses"]))
        elo_diffs.append(float(r["elo_diff"]))

    n = len(labels)
    y = np.arange(n)

    # --- W/D/L horizontal bar chart ---
    fig, ax = plt.subplots(figsize=(12, max(4, n * 0.9)))

    bar_h = 0.6
    ax.barh(y, wins_list, height=bar_h, color="#4CAF50", label="Wins")
    ax.barh(y, draws_list, height=bar_h, left=wins_list,
            color="#9E9E9E", label="Draws")
    left_losses = [w + d for w, d in zip(wins_list, draws_list)]
    ax.barh(y, losses_list, height=bar_h, left=left_losses,
            color="#F44336", label="Losses")

    for i in range(n):
        total = wins_list[i] + draws_list[i] + losses_list[i]
        ax.text(total + 2, y[i],
                f"+{wins_list[i]} ={draws_list[i]} -{losses_list[i]}  "
                f"({elo_diffs[i]:+.0f} ELO)",
                va="center", fontsize=9, fontfamily="monospace")

    ax.set_yticks(y)
    ax.set_yticklabels(labels, fontsize=9)
    ax.invert_yaxis()
    ax.set_xlabel("Games")
    ax.set_title("OmegaZero — SPRT Version Gauntlet")
    ax.legend(loc="lower right")
    ax.grid(True, alpha=0.2, axis="x")

    max_total = max(w + d + l for w, d, l in
                    zip(wins_list, draws_list, losses_list))
    ax.set_xlim(0, max_total * 1.45)

    fig.tight_layout()
    wdl_path = RESULTS_DIR / "sprt_gauntlet_wdl.png"
    fig.savefig(wdl_path, dpi=150)
    plt.close(fig)
    print(f"  W/D/L chart: {wdl_path}")

    # --- ELO diff bar chart ---
    fig, ax = plt.subplots(figsize=(10, max(4, n * 0.8)))

    short_labels = []
    for _, r in gauntlet_rows:
        short_labels.append(f"{r['test_label']}")

    colors = ["#4CAF50" if e > 0 else "#F44336" for e in elo_diffs]
    bars = ax.barh(y, elo_diffs, height=0.55, color=colors)

    for i, (bar, elo) in enumerate(zip(bars, elo_diffs)):
        x_pos = elo + (2 if elo >= 0 else -2)
        ha = "left" if elo >= 0 else "right"
        ax.text(x_pos, y[i], f"{elo:+.1f}", va="center", ha=ha,
                fontsize=9, fontfamily="monospace")

    ax.set_yticks(y)
    ax.set_yticklabels(short_labels, fontsize=9)
    ax.invert_yaxis()
    ax.axvline(0, color="black", linewidth=0.8)
    ax.set_xlabel("ELO difference vs previous version")
    ax.set_title("OmegaZero — ELO Gain Per Version")
    ax.grid(True, alpha=0.2, axis="x")
    fig.tight_layout()

    elo_path = RESULTS_DIR / "sprt_gauntlet_elo.png"
    fig.savefig(elo_path, dpi=150)
    plt.close(fig)
    print(f"  ELO chart:   {elo_path}")


# ---------------------------------------------------------------------------
# run subcommand
# ---------------------------------------------------------------------------

def cmd_run(args):
    test_engine = Path(args.engine)
    if not test_engine.is_absolute():
        test_engine = REPO_ROOT / test_engine
    test_engine = test_engine.resolve()
    if not test_engine.exists():
        sys.exit(f"Test engine not found: {test_engine}\nRun 'make' first.")

    worktree_cleanup = None
    tmpdir_cleanup = None

    if args.baseline_commit:
        baseline_path, tmpdir_cleanup, worktree_path = build_at_commit(
            args.baseline_commit, "base"
        )
        worktree_cleanup = worktree_path
        baseline_label = get_commit_short(args.baseline_commit)
    elif args.baseline:
        baseline_path = args.baseline
        if not Path(baseline_path).is_absolute():
            baseline_path = str(REPO_ROOT / baseline_path)
        if not Path(baseline_path).exists():
            sys.exit(f"Baseline engine not found: {baseline_path}")
        baseline_label = Path(baseline_path).name
    else:
        sys.exit("Specify --baseline <path> or --baseline-commit <ref>")

    cutechess = find_cutechess(args.cutechess)
    test_label = get_version_tag()

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    run_name = f"{ts}_{test_label}_vs_{baseline_label}"
    run_dir = RESULTS_DIR / run_name
    run_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n{'=' * 64}")
    print(f"  SPRT Test: {test_label} vs {baseline_label}")
    print(f"  H0: elo_diff >= {args.elo0}  (accept = no regression)")
    print(f"  H1: elo_diff >= {args.elo1}  (accept = improvement)")
    print(f"  alpha={args.alpha}  beta={args.beta}  |  {args.st}s/move")
    print(f"  Max games: {args.max_games}")
    print(f"{'=' * 64}\n")

    summary, games = run_match(
        cutechess,
        str(test_engine), f"New-{test_label}",
        baseline_path, f"Base-{baseline_label}",
        st=args.st, elo0=args.elo0, elo1=args.elo1,
        alpha=args.alpha, beta=args.beta,
        max_games=args.max_games, concurrency=args.concurrency,
        openings=args.openings, pgn_path=run_dir / "games.pgn",
    )

    if summary is None:
        result_str = "INTERRUPTED"
    else:
        result_str = summary["result"]
        if args.baseline_commit:
            summary["base_commit"] = get_commit_short(args.baseline_commit)
            summary["test_commit"] = get_commit_short("HEAD")

        if games:
            games_csv = run_dir / "games.csv"
            with open(games_csv, "w", newline="") as f:
                w = csv.DictWriter(f, fieldnames=list(games[0].keys()))
                w.writeheader()
                w.writerows(games)

        summary_csv = run_dir / "summary.csv"
        with open(summary_csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=HISTORY_FIELDS)
            w.writeheader()
            w.writerow(summary)

        append_to_history(summary)

        total = summary["games"]
        wins, draws, losses = summary["wins"], summary["draws"], summary["losses"]
        rate = summary["score_rate"]
        elo = summary["elo_diff"]

        print(f"\n{'=' * 64}")
        if result_str == "H1_ACCEPTED":
            print(f"  RESULT: PASSED — new version is stronger")
        elif result_str == "H0_ACCEPTED":
            print(f"  RESULT: FAILED — no significant improvement")
        elif result_str == "INTERRUPTED":
            print(f"  RESULT: INTERRUPTED — test incomplete")
        else:
            print(f"  RESULT: INCONCLUSIVE — max games reached")
        print(f"  {total} games: +{wins} ={draws} -{losses}  "
              f"({rate:.1%})  ELO: {elo:+.1f}")
        print(f"  Results saved to {run_dir}/")
        print(f"{'=' * 64}")

    if worktree_cleanup:
        cleanup_worktree(worktree_cleanup)
    if tmpdir_cleanup:
        shutil.rmtree(tmpdir_cleanup, ignore_errors=True)

    return 0 if result_str == "H1_ACCEPTED" else 1


# ---------------------------------------------------------------------------
# gauntlet subcommand
# ---------------------------------------------------------------------------

def cmd_gauntlet(args):
    cutechess = find_cutechess(args.cutechess)

    tags = discover_version_tags()
    if len(tags) < 2:
        sys.exit(f"Need at least 2 version tags (v1, v2, ...), found: {tags}")

    pairs = list(zip(tags[:-1], tags[1:]))
    done = completed_matchups() if args.resume else set()

    print(f"\n{'=' * 64}")
    print(f"  OmegaZero Version Gauntlet (SPRT)")
    print(f"  Tags found: {', '.join(tags)}")
    print(f"  {len(pairs)} matchups  |  {args.st}s/move")
    print(f"  SPRT bounds: elo0={args.elo0}  elo1={args.elo1}  "
          f"alpha={args.alpha}  beta={args.beta}")
    print(f"  Max games per matchup: {args.max_games}")
    print(f"{'=' * 64}\n")

    for i, (base_tag, test_tag) in enumerate(pairs):
        base_commit = get_commit_short(base_tag)
        test_commit = get_commit_short(test_tag)

        if (base_commit, test_commit) in done:
            print(f"[{i+1}/{len(pairs)}] {test_tag} vs {base_tag} — "
                  f"already done, skipping\n")
            continue

        print(f"[{i+1}/{len(pairs)}] {test_tag} vs {base_tag}")

        base_binary, base_tmp, base_wt = build_at_commit(base_tag, base_tag)
        test_binary, test_tmp, test_wt = build_at_commit(test_tag, test_tag)

        run_dir = RESULTS_DIR / f"{base_tag}_vs_{test_tag}"
        run_dir.mkdir(parents=True, exist_ok=True)

        summary, games = run_match(
            cutechess,
            test_binary, test_tag,
            base_binary, base_tag,
            st=args.st, elo0=args.elo0, elo1=args.elo1,
            alpha=args.alpha, beta=args.beta,
            max_games=args.max_games, concurrency=args.concurrency,
            pgn_path=run_dir / "games.pgn",
        )

        cleanup_worktree(base_wt)
        cleanup_worktree(test_wt)
        shutil.rmtree(base_tmp, ignore_errors=True)
        shutil.rmtree(test_tmp, ignore_errors=True)

        if summary:
            summary["base_commit"] = base_commit
            summary["test_commit"] = test_commit

            if games:
                games_csv = run_dir / "games.csv"
                with open(games_csv, "w", newline="") as f:
                    w = csv.DictWriter(f, fieldnames=list(games[0].keys()))
                    w.writeheader()
                    w.writerows(games)

            append_to_history(summary)

            total = summary["games"]
            w, d, l = summary["wins"], summary["draws"], summary["losses"]
            elo = summary["elo_diff"]
            res = summary["result"]
            status = ("PASSED" if res == "H1_ACCEPTED" else
                      "FAILED" if res == "H0_ACCEPTED" else "INCONCLUSIVE")
            print(f"\n  >> {test_tag} vs {base_tag}: +{w} ={d} -{l}  "
                  f"({total} games)  ELO: {elo:+.1f}  [{status}]\n")
        else:
            print(f"\n  >> Interrupted at {test_tag} vs {base_tag}\n")
            break

    print(f"{'=' * 64}")
    print(f"  Done! Results saved to {HISTORY_CSV}")
    print(f"{'=' * 64}")

    generate_plots()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def add_common_args(parser):
    parser.add_argument("--cutechess", default=None,
                        help="Path to cutechess-cli (default: auto-detect)")
    parser.add_argument("--elo0", type=float, default=0.0,
                        help="H0 bound (default: 0)")
    parser.add_argument("--elo1", type=float, default=10.0,
                        help="H1 bound (default: 10)")
    parser.add_argument("--alpha", type=float, default=0.05,
                        help="Type I error rate (default: 0.05)")
    parser.add_argument("--beta", type=float, default=0.05,
                        help="Type II error rate (default: 0.05)")
    parser.add_argument("--st", default="1",
                        help="Fixed time per move in seconds (default: 1)")
    parser.add_argument("--max-games", type=int, default=10000,
                        help="Maximum games before stopping (default: 10000)")
    parser.add_argument("--concurrency", type=int, default=1,
                        help="Concurrent games (default: 1)")


def main():
    parser = argparse.ArgumentParser(description="OmegaZero SPRT testing")
    sub = parser.add_subparsers(dest="command")

    # --- run ---
    run_p = sub.add_parser("run", help="Run a single SPRT test")
    run_p.add_argument("--engine", default="build/OmegaZero",
                       help="Path to test engine (default: build/OmegaZero)")
    run_p.add_argument("--baseline",
                       help="Path to baseline engine binary")
    run_p.add_argument("--baseline-commit",
                       help="Git ref to build baseline from")
    run_p.add_argument("--openings",
                       help="Path to opening book (.epd or .pgn)")
    add_common_args(run_p)

    # --- gauntlet ---
    gauntlet_p = sub.add_parser("gauntlet",
                                help="SPRT across all version tags (v1→v2→...→vN)")
    gauntlet_p.add_argument("--resume", action="store_true",
                            help="Skip matchups already in sprt_history.csv")
    add_common_args(gauntlet_p)

    # --- plot ---
    sub.add_parser("plot", help="Regenerate plots from sprt_history.csv")

    # --- history ---
    sub.add_parser("history", help="Show SPRT test history")

    args = parser.parse_args()

    if args.command is None:
        if len(sys.argv) > 1:
            args = run_p.parse_args(sys.argv[1:])
            args.command = "run"
        else:
            parser.print_help()
            sys.exit(1)

    if args.command == "run":
        sys.exit(cmd_run(args))
    elif args.command == "gauntlet":
        cmd_gauntlet(args)
    elif args.command == "plot":
        generate_plots()
    elif args.command == "history":
        show_history()


if __name__ == "__main__":
    main()
