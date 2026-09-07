#!/usr/bin/env python3
"""
Analysis plots for the unc-002 eval-error pipeline: raw data and trained heads.

The uncertainty-pipeline analogue of scripts/generate_nnue_plots.py (which has the
same data/model split). The trainer (unc_research/scripts/train_unc_head.py) still
owns the per-run training/calibration figures and their regeneration
(`train_unc_head.py plot <run>`, mirroring train_nnue.py); this script adds the
dataset diagnostics and the deeper trained-head diagnostics.

Subcommands:
    data    — Analyze a preprocessed uncertainty .bin (or a .txt, auto-encoded):
              the u = v - v_star error distribution, |u| by game phase and by
              v_star search depth, and a v vs v_star scatter.
    model   — Deeper diagnostics of a TRAINED head (a run dir with best.bin +
              artifacts.npz): does it learn the heteroscedastic shape (predicted
              width vs |eval| and vs phase), is it calibrated per-region, does its
              conditional mean correct the eval (H6 preview), PIT shape, and
              sharpness-vs-realized reliability. Runs best.bin over the val split.

Output:
    data  — a fresh unc_research/experiment_results/unc_head/<datetime>/figs/ run dir.
    model — writes head_extra_diagnostics.png into the target run's own figs/.
    Both drop a plot_metadata.json with timestamp and git commit.

Usage:
    python3 unc_research/scripts/generate_unc_plots.py data nnue/data/unc_11M/validation_data.bin
    python3 unc_research/scripts/generate_unc_plots.py model unc_research/experiment_results/unc_head/<run>
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

REPO_ROOT = Path(__file__).resolve().parents[2]

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
    out_dir = REPO_ROOT / "unc_research" / "experiment_results" / "unc_head" / ts / "figs"
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


# --------------------------------------------------------------------------- #
#  model: deeper diagnostics of a TRAINED head over the val split
# --------------------------------------------------------------------------- #
def cmd_model(args):
    # torch + the trainer module are heavy; import lazily so `data` never pays.
    import torch
    sys.path.insert(0, str(Path(__file__).resolve().parent))  # train_unc_head is a sibling
    import train_unc_head as T

    def _abs(p):
        p = Path(p)
        return p if p.is_absolute() else REPO_ROOT / p

    run, val, trunk = _abs(args.run), _abs(args.val), _abs(args.trunk)
    art = np.load(run / "artifacts.npz")
    met = json.loads((run / "metrics.json").read_text())
    model, meta = T.read_head_bin(run / "best.bin")
    u_mean, u_std = meta["u_mean_cp"], meta["u_std_cp"]

    recs = np.fromfile(val, dtype=UNC_RECORD_DTYPE)
    v = recs["v"].astype(np.float64)
    u = recs["u"].astype(np.float64)                       # = v - v*
    absv = np.abs(v)
    pieces = recs["num_white"].astype(np.float64) + 2      # total pieces (HalfKP conv.)
    q10 = np.asarray(art["cond_q10_cp"], float)
    q90 = np.asarray(art["cond_q90_cp"], float)
    width = q90 - q10                                       # predicted central-80% width
    n = len(u)
    if len(q10) != n:
        sys.exit(f"val ({n}) != artifacts ({len(q10)}) — pass the matching --val for this run")

    # conditional mean E[u|x] (sum_k pi_k mu_k), de-standardized -> cp
    print("running head for E[u|x] ...")
    emb = T.embed(recs, *T.load_ft(str(trunk)))            # fp16 [n, 2*L1]
    Eu = np.empty(n, np.float64)
    with torch.no_grad():
        for s in range(0, n, 200000):
            e = min(s + 200000, n)
            log_pi, mu, _sig, _df = model.params(torch.from_numpy(emb[s:e]))
            Eu[s:e] = (log_pi.exp() * mu).sum(1).numpy()
    Eu = Eu * u_std + u_mean
    resid_corr = np.abs(u - Eu)                            # |u - E[u]| = |v_hat - v*|

    def binned(x, edges, fn):
        idx = np.digitize(x, edges)
        cx, cy = [], []
        for b in range(1, len(edges)):
            sel = idx == b
            if sel.sum() < 200:
                continue
            cx.append(0.5 * (edges[b - 1] + edges[b]))
            cy.append(fn(sel))
        return np.array(cx), np.array(cy)

    def span80(s):  # empirical central-80% span of u, comparable to predicted width
        return np.percentile(u[s], 90) - np.percentile(u[s], 10)

    plt.rcParams.update({"font.size": 9, "axes.grid": True, "grid.alpha": 0.25})
    fig, ax = plt.subplots(2, 3, figsize=(15, 8.5))
    C, C2, C3 = C_COND, "#E45756", "#54A24B"

    # A: predicted width vs |eval|, against the actual 80% span (learned heteroscedasticity)
    e = np.linspace(0, np.percentile(absv, 99), 22)
    cx, pw = binned(absv, e, lambda s: np.median(width[s]))
    _, aw = binned(absv, e, span80)
    ax[0, 0].plot(cx, pw, "-o", color=C, ms=3, label="predicted 80% width")
    ax[0, 0].plot(cx, aw, "-o", color=C2, ms=3, label="actual 80% span (q90-q10)")
    ax[0, 0].set(title="learned heteroscedasticity vs |eval|", xlabel="|v|  (cp)",
                 ylabel="central-80% width  (cp)")
    ax[0, 0].legend(fontsize=8)

    # B: same vs game phase
    pe = np.linspace(pieces.min(), pieces.max(), 18)
    cx, pw = binned(pieces, pe, lambda s: np.median(width[s]))
    _, aw = binned(pieces, pe, span80)
    ax[0, 1].plot(cx, pw, "-o", color=C, ms=3, label="predicted 80% width")
    ax[0, 1].plot(cx, aw, "-o", color=C2, ms=3, label="actual 80% span (q90-q10)")
    ax[0, 1].set(title="learned heteroscedasticity vs phase",
                 xlabel="pieces on board", ylabel="central-80% width  (cp)")
    ax[0, 1].invert_xaxis()
    ax[0, 1].legend(fontsize=8)

    # C: per-region calibration — central-80% coverage across |eval| bins
    inside = (u >= q10) & (u <= q90)
    cx, cov = binned(absv, e, lambda s: inside[s].mean() * 100)
    ax[0, 2].axhline(80, color="k", ls="--", lw=1, label="nominal 80%")
    ax[0, 2].plot(cx, cov, "-o", color=C3, ms=3, label="empirical")
    ax[0, 2].set(title="per-region calibration (is it uniform?)", xlabel="|v|  (cp)",
                 ylabel="central-80% coverage (%)", ylim=(60, 100))
    ax[0, 2].legend(fontsize=8)

    # D: conditional-mean correction — |u| vs |u - E[u|x]|  (H6 preview)
    gridmax = np.percentile(np.abs(u), 99)
    grid = np.linspace(0, gridmax, 300)
    raw_cdf = np.searchsorted(np.sort(np.abs(u)), grid) / n
    cor_cdf = np.searchsorted(np.sort(resid_corr), grid) / n
    ax[1, 0].plot(grid, raw_cdf, color=C2, label=f"|u| raw (med {np.median(np.abs(u)):.0f})")
    ax[1, 0].plot(grid, cor_cdf, color=C,
                  label=f"|u-E[u|x]| (med {np.median(resid_corr):.0f})")
    ax[1, 0].set(title="mean correction shrinks error (H6 preview)",
                 xlabel="abs error  (cp)", ylabel="CDF", xlim=(0, gridmax))
    ax[1, 0].legend(fontsize=8)

    # E: PIT histograms (U=overconfident, ∩=underconfident)
    ax[1, 1].hist(np.asarray(art["cond_pit"], float), bins=40, density=True, color=C,
                  alpha=0.6, label=f"conditional (KS {met.get('cond_ks', float('nan')):.3f})")
    ax[1, 1].hist(np.asarray(art["unc_pit"], float), bins=40, density=True, color=C2,
                  alpha=0.4, label=f"unconditional (KS {met.get('unc_ks', float('nan')):.3f})")
    ax[1, 1].axhline(1.0, color="k", ls="--", lw=1, label="ideal (uniform)")
    ax[1, 1].set(title="PIT histogram (calibration shape)", xlabel="PIT value",
                 ylabel="density")
    ax[1, 1].legend(fontsize=8)

    # F: sharpness vs realized error — bin by predicted width, show actual span
    qs = np.quantile(width, np.linspace(0, 1, 11))
    qs[-1] += 1e-6
    cx, real = [], []
    for b in range(10):
        sel = (width >= qs[b]) & (width < qs[b + 1])
        if sel.sum() < 200:
            continue
        cx.append(np.median(width[sel]))
        real.append(np.percentile(u[sel], 90) - np.percentile(u[sel], 10))
    lim = max(max(cx), max(real)) * 1.05
    ax[1, 2].plot([0, lim], [0, lim], "k--", lw=0.8, alpha=0.7, label="ideal (y=x)")
    ax[1, 2].plot(cx, real, "-o", color=C3, ms=4, label="realized central-80% span")
    ax[1, 2].set(title="sharpness vs realized error (by width decile)",
                 xlabel="predicted 80% width  (cp)", ylabel="realized 80% span  (cp)",
                 xlim=(0, lim), ylim=(0, lim))
    ax[1, 2].legend(fontsize=8)

    med_raw, med_cor = float(np.median(np.abs(u))), float(np.median(resid_corr))
    fig.suptitle(f"unc-head extra diagnostics — {run.name}   "
                 f"(mean-correction: median |err| {med_raw:.0f} -> {med_cor:.0f} cp)",
                 fontsize=12, y=0.995)
    fig.tight_layout(rect=[0, 0, 1, 0.97])
    out_dir = run / "figs"
    out_dir.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_dir / "head_extra_diagnostics.png", dpi=150, bbox_inches="tight")
    plt.close(fig)
    _save_metadata(out_dir, "model",
                   {"run": str(run), "val": str(val), "positions": int(n),
                    "median_abs_err_raw": med_raw,
                    "median_abs_err_corrected": med_cor})
    print(f"Wrote head_extra_diagnostics.png to {out_dir}/")
    print(f"mean correction: median |u| {med_raw:.1f} -> |u-E[u]| {med_cor:.1f} cp "
          f"({(1 - med_cor / med_raw) * 100:.1f}% reduction)")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    d = sub.add_parser("data", help="analyze a preprocessed uncertainty dataset")
    d.add_argument("input", help="a .bin (or .txt, auto-encoded) uncertainty file")
    d.set_defaults(func=cmd_data)

    m = sub.add_parser("model", help="deeper diagnostics of a trained head over val")
    m.add_argument("run", help="a run dir under unc_research/experiment_results/unc_head/ "
                   "(needs best.bin + artifacts.npz + metrics.json)")
    m.add_argument("--val", default="nnue/data/unc_11M/validation_data.bin",
                   help="validation .bin the run's artifacts were computed on")
    m.add_argument("--trunk", default="nnue/nnue.bin",
                   help="frozen NNUE trunk that produced the embedding (match the run's)")
    m.set_defaults(func=cmd_model)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
