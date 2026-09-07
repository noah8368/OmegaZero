#!/usr/bin/env python3
"""unc008_corrector_diag.py — Milestone E: three-corrector self-play diagnostic.

Reads the corrector-mode datagen output (rows:
  fen | v | v_star | corrhist | model_mean | depth | nodes | result
where corrhist = GetCorrectedEval(v) [live online correction history] and
model_mean = v - E[u|x] [the head's conditional mean]) and compares how close each
corrector gets to the deep-search target v*:

  raw error        = |v          - v*|
  corr-hist error  = |corrhist    - v*|
  model-mean error = |model_mean  - v*|

The decision E asks: does the FROZEN model mean beat the ONLINE correction history
head-to-head (paired, in-distribution self-play)? This is the one comparison unc-007
P2 could not make (val positions are static; corr-hist is path-dependent). A clear
model-mean win green-lights the corrector swap (Phase F); a loss keeps corr-hist.

Writes experiment_results/unc-008/corrector_diag_<ts>/{corrector_diag.png, stats.json}.

Usage:
  python3 unc_research/scripts/unc008_corrector_diag.py --run-dir <datagen output dir>
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
from scipy.stats import wilcoxon

REPO = Path(__file__).resolve().parents[2]
DECISIVE = 20000  # drop mate/TB sentinels
C_RAW = "#9E9E9E"
C_CORR = "#e8820c"
C_MODEL = "#1f77b4"


def load_rows(run_dir):
    """Read every {data,val}_worker_*.txt under run_dir (recursively)."""
    v, vstar, corr, model = [], [], [], []
    # datagen combines the per-worker files into training/validation_data.txt at
    # the end; fall back to worker files if a run is still in progress.
    combined = (sorted(Path(run_dir).rglob("training_data.txt")) +
                sorted(Path(run_dir).rglob("validation_data.txt")))
    files = combined if combined else (
        sorted(Path(run_dir).rglob("data_worker_*.txt")) +
        sorted(Path(run_dir).rglob("val_worker_*.txt")))
    if not files:
        sys.exit(f"no corrector data under {run_dir}")
    for f in files:
        for line in f.read_text().splitlines():
            p = [x.strip() for x in line.split("|")]
            if len(p) < 5:
                continue
            try:
                vv, vs, ch, mm = int(p[1]), int(p[2]), int(p[3]), int(p[4])
            except ValueError:
                continue
            if abs(vs) >= DECISIVE:
                continue
            v.append(vv); vstar.append(vs); corr.append(ch); model.append(mm)
    return (np.array(v, float), np.array(vstar, float),
            np.array(corr, float), np.array(model, float))


def err_stats(pred, vstar):
    e = np.abs(pred - vstar)
    return {"mae": float(e.mean()), "rmse": float(np.sqrt((e ** 2).mean()))}, e


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-dir", required=True, help="corrector-mode datagen output dir")
    ap.add_argument("--out-root", default="unc_research/experiment_results/unc-008")
    a = ap.parse_args()

    v, vstar, corr, model = load_rows(a.run_dir)
    n = len(v)
    print(f"loaded {n} non-decisive corrector positions")

    s_raw, e_raw = err_stats(v, vstar)
    s_corr, e_corr = err_stats(corr, vstar)
    s_model, e_model = err_stats(model, vstar)

    # Paired head-to-head: model-mean vs corr-hist.
    d = e_corr - e_model                      # >0 => model better on that position
    model_win_rate = float((d > 0).mean())
    try:
        w_stat, w_p = wilcoxon(e_model, e_corr)  # two-sided on paired abs errors
    except ValueError:
        w_stat, w_p = float("nan"), float("nan")

    def red(s):  # MAE reduction vs raw
        return 1 - s["mae"] / s_raw["mae"]

    verdict = (
        "model-mean BEATS corr-hist -> green-light swap (Phase F)"
        if s_model["mae"] < s_corr["mae"] and w_p < 0.05 else
        "model-mean ~ corr-hist (no clear winner)"
        if abs(s_model["mae"] - s_corr["mae"]) < 0.5 else
        "corr-hist beats model-mean -> keep corr-hist (head -> margins only)")

    stats = {
        "n": n,
        "mae": {"raw": s_raw["mae"], "corrhist": s_corr["mae"], "model_mean": s_model["mae"]},
        "rmse": {"raw": s_raw["rmse"], "corrhist": s_corr["rmse"], "model_mean": s_model["rmse"]},
        "mae_reduction_vs_raw": {"corrhist": red(s_corr), "model_mean": red(s_model)},
        "paired_model_vs_corrhist": {
            "model_win_rate": model_win_rate,
            "mean_abs_err_diff_corr_minus_model": float(d.mean()),
            "wilcoxon_p": float(w_p),
        },
        "verdict": verdict,
    }

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = Path(a.out_root) / f"corrector_diag_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "stats.json").write_text(json.dumps(stats, indent=2))

    # Figure: (A) MAE/RMSE bars, (B) |error| ECDFs, (C) paired head-to-head.
    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(15, 4.6))
    labels = ["raw", "corr-hist", "model-mean"]
    colors = [C_RAW, C_CORR, C_MODEL]
    maes = [s_raw["mae"], s_corr["mae"], s_model["mae"]]
    rmses = [s_raw["rmse"], s_corr["rmse"], s_model["rmse"]]
    x = np.arange(3)
    ax1.bar(x - 0.2, maes, 0.4, color=colors, label="MAE")
    ax1.bar(x + 0.2, rmses, 0.4, color=colors, alpha=0.5, label="RMSE")
    for i, m in enumerate(maes):
        ax1.text(i - 0.2, m + 1, f"{m:.0f}", ha="center", fontsize=9)
    ax1.set_xticks(x); ax1.set_xticklabels(labels)
    ax1.set_ylabel("error vs deep search v*  (cp)")
    ax1.set_title(f"MAE / RMSE by corrector (n={n})\n"
                  f"corr-hist −{red(s_corr)*100:.0f}%, model-mean −{red(s_model)*100:.0f}% vs raw")
    ax1.legend(fontsize=8)

    for e, c, lab in ((e_raw, C_RAW, "raw"), (e_corr, C_CORR, "corr-hist"),
                      (e_model, C_MODEL, "model-mean")):
        xs = np.sort(e)
        ax2.plot(xs, np.arange(1, len(xs) + 1) / len(xs), color=c, lw=2, label=lab)
    ax2.set_xlim(0, np.percentile(e_raw, 95))
    ax2.set_xlabel("|error vs v*|  (cp)")
    ax2.set_ylabel("cumulative fraction")
    ax2.set_title("error CDF (leftmost = best)")
    ax2.legend(fontsize=8)

    lim = np.percentile(np.concatenate([e_corr, e_model]), 97) + 1
    ax3.hexbin(e_corr, e_model, gridsize=40, cmap="Blues", bins="log",
               extent=(0, lim, 0, lim))
    ax3.plot([0, lim], [0, lim], color="#444", ls="--", lw=1.5)
    ax3.set_xlim(0, lim); ax3.set_ylim(0, lim)
    ax3.set_xlabel("corr-hist |error|  (cp)")
    ax3.set_ylabel("model-mean |error|  (cp)")
    ax3.set_title(f"paired: model-mean better on {model_win_rate*100:.0f}%\n"
                  f"(points below diagonal), wilcoxon p={w_p:.1e}")
    fig.suptitle(f"unc-008 E: three-corrector diagnostic  —  {verdict}", y=1.02)
    fig.tight_layout()
    fig.savefig(out_dir / "corrector_diag.png", dpi=130, bbox_inches="tight")
    plt.close(fig)

    print(f"\nMAE  raw={s_raw['mae']:.1f}  corr-hist={s_corr['mae']:.1f}  "
          f"model-mean={s_model['mae']:.1f}")
    print(f"RMSE raw={s_raw['rmse']:.1f}  corr-hist={s_corr['rmse']:.1f}  "
          f"model-mean={s_model['rmse']:.1f}")
    print(f"reduction vs raw:  corr-hist −{red(s_corr)*100:.1f}%  "
          f"model-mean −{red(s_model)*100:.1f}%")
    print(f"paired model-mean vs corr-hist: model better on {model_win_rate*100:.1f}% "
          f"of positions, wilcoxon p={w_p:.2e}")
    print(f"\nVERDICT: {verdict}")
    print(f"figure -> {out_dir / 'corrector_diag.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
