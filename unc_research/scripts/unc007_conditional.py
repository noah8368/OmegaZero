#!/usr/bin/env python3
"""unc007_conditional.py — conditional calibration add-on to unc-007 P3.

The pooled PIT (unc007_coverage.py) tests only MARGINAL calibration: region-level
miscalibrations can cancel and still look uniform overall. This localizes the
check without needing repeated draws per position (there is only one u per
position) -- by pooling the one-PIT-each over REGIONS of feature space:

  1. PIT-independence tests (continuous, all N):
       Spearman(PIT, feature)         -> conditional MEAN bias (median off in a region)
       Spearman(|PIT-0.5|, feature)   -> conditional DISPERSION error (over/under-
                                          confident in a region: + => more extreme
                                          PITs (over-confident) as the feature grows)
  2. Mondrian bins: quantile-bin each feature, and within each bin report PIT mean,
     KS-vs-uniform, and central coverage -- so a bad region is visible where the
     pooled histogram hides it.

Features: the head's own predicted 80% width, |eval|, and game phase (material).

Reuses the val sampling + harness pass from unc007_coverage. Writes
experiment_results/unc-007/conditional_<ts>/{conditional.png, stats.json}.

Usage:
  make unc_harness
  python3 unc_research/scripts/unc007_conditional.py --n 8000 --bins 5
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
from scipy.stats import kstest, spearmanr

SCRIPTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_DIR))
from unc007_analyze import cdf_cp  # noqa: E402
from unc007_coverage import sample_val, head_mixtures  # noqa: E402

# Levels whose central coverage we track per region.
LEVELS = (0.50, 0.80, 0.90)
LEVEL_COLOR = {0.50: "#7a4fbf", 0.80: "#1f77b4", 0.90: "#e8820c"}


def phase_of(fen):
    """Classic material phase (Q=4,R=2,B/N=1 each side): 24 = full, ~0 = bare."""
    board = fen.split()[0]
    w = {"q": 4, "r": 2, "b": 1, "n": 1}
    return sum(w.get(c.lower(), 0) for c in board if c.isalpha())


def central_coverage(pit, lvl):
    a = 1 - lvl
    return float(((pit >= a / 2) & (pit <= 1 - a / 2)).mean())


def bin_by(feature, pit, nbins):
    """Quantile-bin `feature`; per bin return summary calibration stats."""
    order = np.argsort(feature)
    chunks = np.array_split(order, nbins)
    rows = []
    for idx in chunks:
        p = pit[idx]
        ks = float(kstest(p, "uniform").statistic) if len(p) > 4 else float("nan")
        rows.append({
            "n": int(len(idx)),
            "feat_lo": float(feature[idx].min()),
            "feat_hi": float(feature[idx].max()),
            "feat_median": float(np.median(feature[idx])),
            "pit_mean": float(p.mean()),
            "ks": ks,
            "coverage": {f"{l:.2f}": central_coverage(p, l) for l in LEVELS},
        })
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--val", default="nnue/data/unc_11M/validation_data.txt")
    ap.add_argument("--net", default="unc_research/models/nnue_unc.bin")
    ap.add_argument("--harness", default="build/unc_harness")
    ap.add_argument("--n", type=int, default=8000)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--bins", type=int, default=5)
    ap.add_argument("--out-root", default="unc_research/experiment_results/unc-007")
    a = ap.parse_args()

    rows = sample_val(a.val, a.n, a.seed)
    fens = [f for f, _ in rows]
    print(f"sampled {len(rows)} non-decisive val positions")
    mix = head_mixtures(a.harness, a.net, fens)

    pit, feats = [], {"pred_width_cp": [], "abs_eval_cp": [], "phase": []}
    for fen, u in rows:
        if fen not in mix:
            continue
        m = mix[fen]
        pit.append(cdf_cp(m, u))
        feats["pred_width_cp"].append(m["q"]["0.90"] - m["q"]["0.10"])
        feats["abs_eval_cp"].append(abs(m["v"]))
        feats["phase"].append(phase_of(fen))
    pit = np.array(pit)
    feats = {k: np.array(v, dtype=float) for k, v in feats.items()}
    n = len(pit)

    # (1) Independence tests: mean-bias and dispersion-error vs each feature.
    dispersion = np.abs(pit - 0.5)
    indep = {}
    for k, f in feats.items():
        rm, pm = spearmanr(f, pit)
        rd, pd = spearmanr(f, dispersion)
        indep[k] = {
            "spearman_pit_r": float(rm), "spearman_pit_p": float(pm),
            "spearman_absdev_r": float(rd), "spearman_absdev_p": float(pd),
            "dispersion_reading": (
                "over-confident grows with feature" if rd > 0 and pd < 0.05 else
                "over-dispersed grows with feature" if rd < 0 and pd < 0.05 else
                "no significant dispersion trend"),
        }

    # (2) Mondrian bins per feature.
    binned = {k: bin_by(feats[k], pit, a.bins) for k in feats}

    stats = {"n": n, "bins": a.bins, "pit_mean_overall": float(pit.mean()),
             "independence": indep, "mondrian": binned,
             "note": "val split, in-distribution; one PIT per position pooled "
                     "within feature-quantile regions"}

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = Path(a.out_root) / f"conditional_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "stats.json").write_text(json.dumps(stats, indent=2))

    # Figure: per feature, central coverage vs the feature (nominal = dashed).
    labels = {"pred_width_cp": "predicted 80% width (cp)",
              "abs_eval_cp": "|eval| (cp)", "phase": "game phase (material)"}
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.4))
    for ax, k in zip(axes, feats):
        xs = [b["feat_median"] for b in binned[k]]
        for lvl in LEVELS:
            ys = [b["coverage"][f"{lvl:.2f}"] for b in binned[k]]
            ax.plot(xs, ys, "-o", color=LEVEL_COLOR[lvl], label=f"{int(lvl*100)}%")
            ax.axhline(lvl, color=LEVEL_COLOR[lvl], ls="--", lw=1, alpha=0.6)
        ax.set_xlabel(labels[k])
        ax.set_ylabel("central coverage (empirical)")
        rd = indep[k]["spearman_absdev_r"]
        ax.set_title(f"{k}\n|PIT-.5| trend r={rd:+.2f} "
                     f"({indep[k]['dispersion_reading']})", fontsize=9)
        ax.set_ylim(0.3, 1.02)
        ax.legend(fontsize=8, title="nominal")
    fig.suptitle(f"unc-007 conditional calibration: coverage across regions "
                 f"(n={n}, {a.bins} bins/feature)", y=1.03)
    fig.tight_layout()
    fig.savefig(out_dir / "conditional.png", dpi=130, bbox_inches="tight")
    plt.close(fig)

    print(f"\nPIT-independence (Spearman):")
    for k in feats:
        d = indep[k]
        print(f"  {k:14}  mean-bias r={d['spearman_pit_r']:+.3f} (p={d['spearman_pit_p']:.1e})   "
              f"dispersion r={d['spearman_absdev_r']:+.3f} (p={d['spearman_absdev_p']:.1e})  "
              f"[{d['dispersion_reading']}]")
    print(f"\nfigure  -> {out_dir / 'conditional.png'}")
    print(f"stats   -> {out_dir / 'stats.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
