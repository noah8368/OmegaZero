#!/usr/bin/env python3
"""
Generate analysis plots for the NF-002 uncertainty head and its data.

The analogue of scripts/plot_training.py for the uncertainty (eval-error) pipeline.
Two subcommands:
    head    — Re-render a trainer run's calibration/training figures from the
              artifacts research/experiments/train_unc_head.py saves (no retrain).
    data    — Analyze a preprocessed uncertainty .bin (or a .txt, auto-encoded):
              the u = v - v_star error distribution, |u| by game phase and by
              v_star search depth, and a v vs v_star scatter.

The `head` figures are also emitted automatically at the end of a training run;
this script lets you regenerate or tweak them from saved artifacts, and analyze
raw datasets without training. render_head_plots() is imported by the trainer.

Output:
    <run_dir>/figs/                                — head subcommand plots (reads the
                                                     run's artifacts.npz alongside)
    research/experiment_results/unc_data_analysis/ — data subcommand plots
    Each dir includes a plot_metadata.json with timestamp and git commit.

Usage:
    python3 scripts/plot_unc_head_performance.py head research/experiment_results/unc_head/<run>/
    python3 scripts/plot_unc_head_performance.py data nnue/data_uncertainty/combined/validation_data.bin
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
C_FLOOR = "#ff7f0e"   # unconditional floor (orange)
C_REF = "#9E9E9E"     # reference line / ideal
C_DATA = "#4C72B0"    # raw-data histograms


# --------------------------------------------------------------------------- #
#  Metadata helpers (mirror plot_training.py)
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
#  head: trainer calibration / training figures (from saved artifacts)
# --------------------------------------------------------------------------- #
def render_head_plots(out_dir, artifacts, meta=None):
    """Render the uncertainty-head figures into out_dir. `artifacts` holds the
    arrays the trainer computed; see save_head_artifacts() for the schema. Called
    both inline by the trainer and by the `head` CLI (from a saved artifacts.npz).
    Best-effort: never raises on a plotting problem, just reports it."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    levels = [0.50, 0.80, 0.90, 0.95]
    written = []

    # 1. Loss curves: val NLL per epoch, conditional vs floor (train NLL faint).
    fig, ax = plt.subplots(figsize=(9, 6))
    for hist, color, label in ((artifacts["cond_hist"], C_COND, "conditional"),
                               (artifacts["unc_hist"], C_FLOOR, "unconditional floor")):
        hist = np.asarray(hist, dtype=float)
        if hist.size == 0:
            continue
        ep = np.arange(1, len(hist) + 1)
        ax.plot(ep, hist[:, 1], color=color, label=f"{label} (val)")
        ax.plot(ep, hist[:, 0], color=color, alpha=0.35, linestyle="--",
                label=f"{label} (train)")
        best = int(np.argmin(hist[:, 1]))
        ax.scatter([best + 1], [hist[best, 1]], color=color, zorder=5, s=40)
    ax.set_xlabel("epoch")
    ax.set_ylabel("NLL (nats, standardized u)")
    ax.set_title("Uncertainty head — training NLL (lower is better)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    p = out_dir / "head_loss_curves.png"
    fig.savefig(p, dpi=150, bbox_inches="tight")
    plt.close(fig)
    written.append(p)

    # 2. PIT reliability: sorted PIT vs uniform quantiles; on-diagonal = calibrated.
    fig, ax = plt.subplots(figsize=(6.5, 6.5))
    ax.plot([0, 1], [0, 1], color=C_REF, linestyle="--", label="ideal (uniform)")
    for key, color, label, ks in (
            ("cond_pit", C_COND, "conditional", meta and meta.get("cond_ks")),
            ("unc_pit", C_FLOOR, "unconditional floor", meta and meta.get("unc_ks"))):
        pit = np.sort(np.asarray(artifacts[key], dtype=float))
        if pit.size == 0:
            continue
        u = np.arange(1, len(pit) + 1) / len(pit)
        lab = label if ks is None else f"{label} (KS={ks:.3f})"
        ax.plot(pit, u, color=color, label=lab)
    ax.set_xlabel("PIT value")
    ax.set_ylabel("empirical CDF")
    ax.set_title("PIT reliability — deviation from diagonal = miscalibration")
    ax.legend()
    ax.grid(True, alpha=0.3)
    p = out_dir / "head_pit_reliability.png"
    fig.savefig(p, dpi=150, bbox_inches="tight")
    plt.close(fig)
    written.append(p)

    # 3. Coverage calibration: nominal vs empirical central-interval coverage.
    fig, ax = plt.subplots(figsize=(6.5, 6.5))
    ax.plot([0, 1], [0, 1], color=C_REF, linestyle="--", label="ideal")
    for key, color, label in (("cond_coverage", C_COND, "conditional"),
                              ("unc_coverage", C_FLOOR, "unconditional floor")):
        cov = artifacts.get(key)
        if cov is None:
            continue
        emp = np.asarray(cov, dtype=float)  # aligned to `levels`
        ax.plot(levels, emp, "o-", color=color, label=label)
    ax.set_xlabel("nominal central-interval coverage")
    ax.set_ylabel("empirical coverage")
    ax.set_title("Coverage calibration")
    ax.legend()
    ax.grid(True, alpha=0.3)
    p = out_dir / "head_coverage.png"
    fig.savefig(p, dpi=150, bbox_inches="tight")
    plt.close(fig)
    written.append(p)

    # 4. Predicted-uncertainty distribution: per-position 80% interval width (cp).
    #    A spread here = heteroscedasticity (the head assigns position-dependent
    #    uncertainty); the floor is a single constant width for reference.
    q10 = np.asarray(artifacts["cond_q10_cp"], dtype=float)
    q90 = np.asarray(artifacts["cond_q90_cp"], dtype=float)
    if q10.size and q90.size:
        width = np.clip(q90 - q10, 0, np.percentile(q90 - q10, 99.5))
        fig, ax = plt.subplots(figsize=(9, 6))
        ax.hist(width, bins=80, color=C_COND, alpha=0.8, edgecolor="none")
        ax.axvline(float(np.median(width)), color="black", linestyle="--",
                   label=f"median {np.median(width):.0f}cp")
        fw = artifacts.get("unc_width80_cp")
        if fw is not None:
            ax.axvline(float(fw), color=C_FLOOR, linestyle="-",
                       label=f"floor (constant) {float(fw):.0f}cp")
        ax.set_xlabel("predicted central-80% interval width (cp)")
        ax.set_ylabel("positions")
        ax.set_title("Predicted uncertainty per position (heteroscedasticity)")
        ax.legend()
        ax.grid(True, alpha=0.3)
        p = out_dir / "head_uncertainty_distribution.png"
        fig.savefig(p, dpi=150, bbox_inches="tight")
        plt.close(fig)
        written.append(p)

    return written


def save_head_artifacts(out_dir, cond_hist, unc_hist, cond_cal, unc_cal,
                        scalars):
    """Persist everything the head plots need so plot_unc_head_performance.py can
    regenerate them without retraining. Writes artifacts.npz + metrics.json (to the
    run dir; plots render into <run>/figs/)."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    levels = [0.50, 0.80, 0.90, 0.95]
    np.savez(
        out_dir / "artifacts.npz",
        cond_hist=np.asarray(cond_hist, dtype=float),
        unc_hist=np.asarray(unc_hist, dtype=float),
        cond_pit=cond_cal["pit"], unc_pit=unc_cal["pit"],
        cond_coverage=np.array([cond_cal["coverage"][l] for l in levels]),
        unc_coverage=np.array([unc_cal["coverage"][l] for l in levels]),
        cond_q10_cp=cond_cal["q_cp"][0.10], cond_q90_cp=cond_cal["q_cp"][0.90],
        unc_width80_cp=np.array(
            float(np.median(unc_cal["q_cp"][0.90] - unc_cal["q_cp"][0.10]))),
    )
    (out_dir / "metrics.json").write_text(json.dumps(scalars, indent=2))


def cmd_head(args):
    run_dir = Path(args.run_dir)
    npz = run_dir / "artifacts.npz"
    if not npz.exists():
        sys.exit(f"No artifacts.npz in {run_dir} (was the run trained with plots?)")
    data = np.load(npz)
    artifacts = {k: data[k] for k in data.files}
    # unc_width80_cp saved as 0-d array -> scalar.
    if "unc_width80_cp" in artifacts:
        artifacts["unc_width80_cp"] = float(artifacts["unc_width80_cp"])
    meta = {}
    mpath = run_dir / "metrics.json"
    if mpath.exists():
        m = json.loads(mpath.read_text())
        meta = {"cond_ks": m.get("cond_ks"), "unc_ks": m.get("unc_ks")}
    figs = run_dir / "figs"
    written = render_head_plots(figs, artifacts, meta)
    _save_metadata(figs, "head", {"plots": [p.name for p in written]})
    print(f"Wrote {len(written)} head plots to {figs}/")
    for p in written:
        print(f"  {p.name}")


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

    out_dir = REPO_ROOT / "research" / "experiment_results" / "unc_data_analysis"
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

    h = sub.add_parser("head", help="re-render a trainer run's figures")
    h.add_argument("run_dir", help="research/experiment_results/unc_head/<run>/ (has artifacts.npz)")
    h.set_defaults(func=cmd_head)

    d = sub.add_parser("data", help="analyze a preprocessed uncertainty dataset")
    d.add_argument("input", help="a .bin (or .txt, auto-encoded) uncertainty file")
    d.set_defaults(func=cmd_data)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
