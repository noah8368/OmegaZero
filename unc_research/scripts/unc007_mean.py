#!/usr/bin/env python3
"""unc007_mean.py — powered P2 decision: is E[u|x] a useful eval corrector?

The curated P2 (n=21, OOD) was weak (r~0.2), but low correlation does NOT mean a
useless mean: even a perfect conditional mean has modest r when u = v - v* carries
large aleatoric (tactical) variance. The decision metric is how much the mean
REDUCES error, plus whether it is a calibrated mean.

Corrected eval = v - E[u|x]; its error vs deep search is (u - E[u|x]). So:
  MAE_raw  = mean|u|              (raw eval's distance to v*)
  MAE_corr = mean|u - E[u|x]|     (corrected eval's distance to v*)
  reduction = 1 - MAE_corr/MAE_raw   <- the H6 value, decision-relevant.

Also:
  - variance explained  R2 = 1 - Var(u - E[u]) / Var(u)  (predictive; can be <0)
  - Pearson/Spearman (direction/monotonicity)
  - mean-reliability: bin by E[u|x], plot mean realized u per bin vs the diagonal
    (on-diagonal => E[u|x] IS the true conditional mean, even if r is modest)
  - the same, split by predicted-width regime (does it correct quiet > sharp?).

Powered, in-distribution (val split, held out from gradients). Writes
experiment_results/unc-007/mean_<ts>/{mean.png, stats.json}.

Usage:
  make unc_harness
  python3 unc_research/scripts/unc007_mean.py --n 20000
"""

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.stats import pearsonr, spearmanr

SCRIPTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_DIR))
from unc007_coverage import sample_val, head_mixtures  # noqa: E402

C_PT = "#1f77b4"
C_REF = "#9E9E9E"


def reduction_stats(u, e):
    """Error-reduction metrics for corrected (u-e) vs raw (u)."""
    mae_raw, mae_corr = float(np.mean(np.abs(u))), float(np.mean(np.abs(u - e)))
    rmse_raw = float(np.sqrt(np.mean(u ** 2)))
    rmse_corr = float(np.sqrt(np.mean((u - e) ** 2)))
    r2 = 1.0 - np.var(u - e) / np.var(u)
    return {
        "mae_raw": mae_raw, "mae_corr": mae_corr,
        "mae_reduction": 1 - mae_corr / mae_raw,
        "rmse_raw": rmse_raw, "rmse_corr": rmse_corr,
        "rmse_reduction": 1 - rmse_corr / rmse_raw,
        "variance_explained_r2": float(r2),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--val", default="nnue/data/unc_11M/validation_data.txt")
    ap.add_argument("--net", default="unc_research/models/nnue_unc.bin")
    ap.add_argument("--harness", default="build/unc_harness")
    ap.add_argument("--n", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--bins", type=int, default=12)
    ap.add_argument("--out-root", default="unc_research/experiment_results/unc-007")
    a = ap.parse_args()

    rows = sample_val(a.val, a.n, a.seed)
    fens = [f for f, _ in rows]
    print(f"sampled {len(rows)} non-decisive val positions")
    mix = head_mixtures(a.harness, a.net, fens)

    u, e, width = [], [], []
    for fen, uu in rows:
        if fen not in mix:
            continue
        m = mix[fen]
        u.append(uu)
        e.append(m["e_u_cp"])
        width.append(m["q"]["0.90"] - m["q"]["0.10"])
    u = np.array(u, dtype=float)
    e = np.array(e, dtype=float)
    width = np.array(width, dtype=float)
    n = len(u)

    overall = reduction_stats(u, e)
    pear_r, pear_p = pearsonr(e, u)
    spr_r, spr_p = spearmanr(e, u)
    overall.update({"pearson_r": float(pear_r), "pearson_p": float(pear_p),
                    "spearman_r": float(spr_r), "spearman_p": float(spr_p)})

    # Mean reliability: bin by E[u], compare mean realized u vs bin's mean E[u].
    order = np.argsort(e)
    rel = []
    for idx in np.array_split(order, a.bins):
        rel.append({"e_mean": float(e[idx].mean()), "u_mean": float(u[idx].mean()),
                    "n": int(len(idx))})

    # Correction by uncertainty regime: MAE-reduction in predicted-width terciles.
    wq = np.quantile(width, [1/3, 2/3])
    regimes = {"quiet(low width)": width <= wq[0],
               "mid": (width > wq[0]) & (width <= wq[1]),
               "sharp(high width)": width > wq[1]}
    by_regime = {name: {**reduction_stats(u[m], e[m]),
                        "n": int(m.sum()),
                        "median_width_cp": float(np.median(width[m]))}
                 for name, m in regimes.items()}

    verdict = (
        "USEFUL corrector" if overall["mae_reduction"] >= 0.05 and pear_r > 0.2 else
        "marginal corrector" if overall["mae_reduction"] > 0.0 else
        "NOT a useful corrector (does not beat raw eval)")
    stats = {"n": n, "overall": overall, "verdict": verdict,
             "mean_reliability_bins": rel, "by_width_regime": by_regime,
             "note": "val split, in-distribution (held out from gradients); "
                     "corrected eval = v - E[u|x], error vs deep-search v*"}

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = Path(a.out_root) / f"mean_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "stats.json").write_text(json.dumps(stats, indent=2))

    # Figure: (A) mean-reliability, (B) MAE raw vs corrected by width regime.
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.6))
    lim = np.percentile(np.abs(np.concatenate([e, u])), 98)
    ax1.hexbin(e, u, gridsize=45, cmap="Blues", bins="log",
               extent=(-lim, lim, -lim, lim))
    em = np.array([r["e_mean"] for r in rel])
    um = np.array([r["u_mean"] for r in rel])
    ax1.plot(em, um, "o-", color="#e8820c", lw=2, label="binned mean realized u")
    ax1.plot([-lim, lim], [-lim, lim], color=C_REF, ls="--", label="ideal (calibrated mean)")
    ax1.set_xlim(-lim, lim); ax1.set_ylim(-lim, lim)
    ax1.set_xlabel("predicted mean  E[u|x]  (cp)")
    ax1.set_ylabel("realized  u = v - v*  (cp)")
    ax1.set_title(f"mean reliability (n={n})\n"
                  f"pearson r={pear_r:.2f}, var-explained R2={overall['variance_explained_r2']:.3f}")
    ax1.legend(fontsize=8, loc="upper left")

    names = list(by_regime)
    xr = np.arange(len(names))
    ax2.bar(xr - 0.2, [by_regime[k]["mae_raw"] for k in names], 0.4,
            color=C_REF, label="raw |u|")
    ax2.bar(xr + 0.2, [by_regime[k]["mae_corr"] for k in names], 0.4,
            color=C_PT, label="corrected |u - E[u]|")
    for i, k in enumerate(names):
        ax2.text(i, max(by_regime[k]["mae_raw"], by_regime[k]["mae_corr"]) + 3,
                 f"-{by_regime[k]['mae_reduction']*100:.0f}%", ha="center", fontsize=9)
    ax2.set_xticks(xr); ax2.set_xticklabels(names, fontsize=8)
    ax2.set_ylabel("MAE vs deep search v*  (cp)")
    ax2.set_title(f"correction by uncertainty regime\noverall MAE reduction "
                  f"{overall['mae_reduction']*100:.1f}%")
    ax2.legend(fontsize=8)
    fig.suptitle(f"unc-007 P2: E[u|x] as an eval corrector  —  {verdict}", y=1.02)
    fig.tight_layout()
    fig.savefig(out_dir / "mean.png", dpi=130, bbox_inches="tight")
    plt.close(fig)

    print(f"\nP2 (val, n={n}):  {verdict}")
    print(f"  pearson r={pear_r:.3f}  spearman r={spr_r:.3f}  var-explained R2={overall['variance_explained_r2']:.3f}")
    print(f"  MAE  raw={overall['mae_raw']:.1f}  corrected={overall['mae_corr']:.1f}  "
          f"reduction={overall['mae_reduction']*100:.1f}%")
    print(f"  RMSE raw={overall['rmse_raw']:.1f}  corrected={overall['rmse_corr']:.1f}  "
          f"reduction={overall['rmse_reduction']*100:.1f}%")
    print("  by predicted-width regime (MAE reduction):")
    for k in names:
        print(f"    {k:20} n={by_regime[k]['n']:5}  raw={by_regime[k]['mae_raw']:6.1f}  "
              f"corr={by_regime[k]['mae_corr']:6.1f}  -{by_regime[k]['mae_reduction']*100:.1f}%")
    print(f"\nfigure -> {out_dir / 'mean.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
