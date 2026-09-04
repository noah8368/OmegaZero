#!/usr/bin/env python3
"""
Analyze a preprocessed uncertainty dataset for the unc-002 eval-error pipeline.

The uncertainty-pipeline analogue of scripts/generate_nnue_plots.py: a standalone
analysis tool over the raw data, separate from the trainer. The trainer
(research/experiments/train_unc_head.py) owns the training/calibration figures for
a run and their regeneration (`train_unc_head.py plot <run>`), mirroring how
train_nnue.py owns its own plots; this script analyzes datasets.

One subcommand:
    data    — Analyze a preprocessed uncertainty .bin (or a .txt, auto-encoded):
              the u = v - v_star error distribution, |u| by game phase and by
              v_star search depth, and a v vs v_star scatter.

Output:
    Writes into a fresh research/experiment_results/unc_head/<datetime>/figs/ (each
    invocation its own timestamped run), including a plot_metadata.json with
    timestamp and git commit.

Usage:
    python3 scripts/generate_unc_head_plots.py data nnue/data/unc_11M/validation_data.bin
"""

import argparse
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_unc_data import UNC_RECORD_DTYPE, encode_uncertainty  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]

# Consistent, colorblind-friendly roles across every figure.
C_COND = "#1f77b4"    # conditional model (blue)
C_REF = "#9E9E9E"     # reference line / ideal
C_DATA = "#4C72B0"    # raw-data histograms


# --------------------------------------------------------------------------- #
#  Metadata helpers (mirror generate_nnue_plots.py)
# --------------------------------------------------------------------------- #
def _git_hash():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=REPO_ROOT,
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"


def _save_metadata(out_dir, command, extra=None):
    meta = {"timestamp": datetime.now().isoformat(), "git_commit": _git_hash(),
            "command": command}
    if extra:
        meta.update(extra)
    (Path(out_dir) / "plot_metadata.json").write_text(json.dumps(meta, indent=2))


# --------------------------------------------------------------------------- #
#  data: dataset diagnostics from a preprocessed uncertainty .bin
# --------------------------------------------------------------------------- #
def cmd_data(args):
    path = Path(args.input)
    if not path.is_absolute():
        path = REPO_ROOT / path
    if path.suffix == ".txt":
        binp = path.with_suffix(".bin")
        if not binp.exists() or path.stat().st_mtime > binp.stat().st_mtime:
            print(f"Encoding {path.name} -> {binp.name} ...")
            encode_uncertainty(path, binp)
        path = binp
    recs = np.fromfile(path, dtype=UNC_RECORD_DTYPE)
    if len(recs) == 0:
        sys.exit(f"No records in {path}")

    # Dataset diagnostics share the unc_head/<datetime>/figs/ convention with the
    # trainer (each invocation is its own timestamped run).
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = REPO_ROOT / "research" / "experiment_results" / "unc_head" / ts / "figs"
    out_dir.mkdir(parents=True, exist_ok=True)

    u = recs["u"].astype(np.float64)
    v = recs["v"].astype(np.float64)
    vstar = recs["v_star"].astype(np.float64)
    depth = recs["depth"].astype(np.int32)
    # In HalfKP each perspective enumerates the SAME non-king pieces, so
    # num_white == num_black == (#non-king pieces); total pieces = that + 2 kings.
    pieces = recs["num_white"].astype(np.int32) + 2

    # 1. u = v - v_star distribution (the modeled eval error).
    fig, ax = plt.subplots(figsize=(10, 6))
    clip = np.clip(u, -800, 800)
    ax.hist(clip, bins=120, color=C_DATA, alpha=0.85, edgecolor="none")
    ax.axvline(0, color=C_REF, linestyle="--")
    ax.axvline(float(np.mean(u)), color="#C44E52", linestyle="-",
               label=f"mean {np.mean(u):.1f}cp")
    ax.set_xlabel("u = v − v*  (cp, STM POV; clipped ±800)")
    ax.set_ylabel("positions")
    ax.set_title(f"Eval-error distribution ({len(recs):,} positions, "
                 f"std {np.std(u):.0f}cp)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "unc_data_u_distribution.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 2. |u| by game phase (piece count): where is the eval least certain?
    fig, ax = plt.subplots(figsize=(10, 6))
    pcs = np.arange(pieces.min(), pieces.max() + 1)
    mean_abs = [np.mean(np.abs(u[pieces == pc])) if np.any(pieces == pc) else np.nan
                for pc in pcs]
    ax.bar(pcs, mean_abs, color=C_DATA, alpha=0.85)
    ax.set_xlabel("pieces on board (endgame → opening)")
    ax.set_ylabel("mean |u| (cp)")
    ax.set_title("Eval error by game phase")
    ax.grid(True, alpha=0.3, axis="y")
    fig.savefig(out_dir / "unc_data_abs_u_by_phase.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 3. |u| by v_star search depth: deeper targets => which error regime?
    fig, ax = plt.subplots(figsize=(10, 6))
    ds = np.arange(depth.min(), depth.max() + 1)
    mean_abs_d = [np.mean(np.abs(u[depth == d])) if np.any(depth == d) else np.nan
                  for d in ds]
    counts = [int(np.sum(depth == d)) for d in ds]
    ax.bar(ds, mean_abs_d, color=C_COND, alpha=0.85)
    ax.set_xlabel("v* search depth")
    ax.set_ylabel("mean |u| (cp)")
    ax.set_title("Eval error by target search depth")
    for d, c, m in zip(ds, counts, mean_abs_d):
        if not np.isnan(m):
            ax.annotate(f"{c:,}", (d, m), ha="center", va="bottom", fontsize=7,
                        color="#555")
    ax.grid(True, alpha=0.3, axis="y")
    fig.savefig(out_dir / "unc_data_abs_u_by_depth.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 4. v vs v_star: static eval against the deep target.
    fig, ax = plt.subplots(figsize=(7, 7))
    lim = 1000
    ax.hist2d(np.clip(v, -lim, lim), np.clip(vstar, -lim, lim), bins=120,
              cmap="viridis", cmin=1)
    ax.plot([-lim, lim], [-lim, lim], color="white", linestyle="--", alpha=0.7)
    ax.set_xlabel("v  (static eval, cp)")
    ax.set_ylabel("v*  (deep target, cp)")
    ax.set_title("Static eval vs deep target (clipped ±1000)")
    fig.savefig(out_dir / "unc_data_v_vs_vstar.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    _save_metadata(out_dir, "data",
                   {"input": str(path), "positions": int(len(recs))})
    print(f"Wrote 4 data plots to {out_dir}/")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    d = sub.add_parser("data", help="analyze a preprocessed uncertainty dataset")
    d.add_argument("input", help="a .bin (or .txt, auto-encoded) uncertainty file")
    d.set_defaults(func=cmd_data)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
