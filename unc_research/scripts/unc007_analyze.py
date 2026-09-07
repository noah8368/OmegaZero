#!/usr/bin/env python3
"""unc007_analyze.py — Phase C: qualitative plots + quantitative verdicts.

Consumes a Phase-B results.jsonl (curated positions with v, v*, u, and the
predicted p(u|x) mixture) and produces:

  C1  analytic reconstruction of each p(u|x) from its mixture params (cp space).
  C2  a small-multiples figure grouped by category (density over u; lines at
      u=0, E[u|x], realized u; shaded central band; v / corrected-eval / PIT).
  C3  the pre-registered verdicts:
        P1 spread ordering  -- predicted 80% width: wide > tight (Mann-Whitney)
        P2 mean direction   -- does E[u|x] track the realized error u? (corr +
                               sign agreement; the H6 corrector preview)
        P3 coverage / PIT    -- realized u's PIT ~ Uniform (central coverage),
                               a COARSE check at this N (curated set is small).

Writes <run>/figs/curated_pux.png + <run>/metrics.json (+ plot_metadata.json).

Usage:
  python3 unc_research/scripts/unc007_analyze.py            # latest unc-007 run
  python3 unc_research/scripts/unc007_analyze.py --run <dir>
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
from matplotlib.patches import Patch
from scipy.stats import t as student_t, mannwhitneyu, pearsonr, spearmanr

REPO = Path(__file__).resolve().parents[2]
DEFAULT_ROOT = REPO / "unc_research" / "experiment_results" / "unc-007"

# Category roles (colorblind-friendly): tight=green, wide=orange, biased=purple.
CAT_COLOR = {"tight": "#2ca02c", "wide": "#e8820c", "biased": "#7a4fbf"}
CAT_ORDER = ["tight", "wide", "biased"]
C_REALIZED = "#111111"   # realized u (ground truth)
C_ZERO = "#9E9E9E"       # u = 0 reference


def _git_hash():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=REPO,
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"


def latest_run(root):
    runs = [p for p in Path(root).glob("*/results.jsonl")]
    if not runs:
        sys.exit(f"no results.jsonl under {root} (run unc007_harness.py first)")
    return sorted(runs, key=lambda p: p.parent.name)[-1].parent


def load_rows(run_dir):
    rows = []
    for line in (run_dir / "results.jsonl").read_text().splitlines():
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


# --- mixture reconstruction (cp space) -------------------------------------- #
def _std_params(row):
    return (np.array(row["pi"]), np.array(row["mu"]),
            np.array(row["sigma"]), np.array(row["df"]),
            row["u_mean"], row["u_std"])


def pdf_cp(row, u_cp):
    """p(u|x) density at cp values u_cp (Jacobian 1/u_std from standardization)."""
    pi, mu, sig, df, um, us = _std_params(row)
    y = (np.asarray(u_cp)[:, None] - um) / us
    comp = student_t.pdf((y - mu) / sig, df) / sig
    return (pi * comp).sum(axis=1) / us


def cdf_cp(row, u_cp):
    pi, mu, sig, df, um, us = _std_params(row)
    y = (float(u_cp) - um) / us
    return float((pi * student_t.cdf((y - mu) / sig, df)).sum())


def spread80(row):
    """Predicted central 80% width (cp) -- the P1 spread metric."""
    return row["q"]["0.90"] - row["q"]["0.10"]


# --- C3 metrics ------------------------------------------------------------- #
def compute_metrics(rows):
    usable = [r for r in rows if not r.get("decisive")]
    by_cat = {c: [r for r in usable if r["category"] == c] for c in CAT_ORDER}

    # P1: predicted 80% width by category; one-sided Mann-Whitney wide > tight.
    widths = {c: [spread80(r) for r in by_cat[c]] for c in CAT_ORDER}
    p1 = {"width_cp_mean": {c: float(np.mean(widths[c])) if widths[c] else None
                            for c in CAT_ORDER},
          "width_cp_median": {c: float(np.median(widths[c])) if widths[c] else None
                              for c in CAT_ORDER}}
    if widths["wide"] and widths["tight"]:
        u_stat, p_val = mannwhitneyu(widths["wide"], widths["tight"],
                                     alternative="greater")
        p1["wide_gt_tight_mannwhitney_p"] = float(p_val)
        p1["verdict"] = ("wide > tight (p<0.05)" if p_val < 0.05
                         else "no significant spread ordering")

    # P2: does E[u|x] track realized u? correlation + sign agreement.
    e = np.array([r["e_u_cp"] for r in usable])
    u = np.array([r["u"] for r in usable], dtype=float)
    p2 = {}
    if len(u) >= 3:
        p2["pearson_r"], p2["pearson_p"] = (float(v) for v in pearsonr(e, u))
        p2["spearman_r"], p2["spearman_p"] = (float(v) for v in spearmanr(e, u))
    # Sign agreement on positions where the error is non-trivial (|u| >= 20 cp).
    sig_mask = np.abs(u) >= 20.0
    if sig_mask.any():
        agree = np.sign(e[sig_mask]) == np.sign(u[sig_mask])
        p2["sign_agree_rate"] = float(agree.mean())
        p2["sign_agree_n"] = int(sig_mask.sum())

    # P3: PIT of realized u; central coverage (COARSE at this N).
    pit = np.array([cdf_cp(r, r["u"]) for r in usable])
    cov = {}
    for lvl in (0.50, 0.80, 0.90):
        a = 1 - lvl
        cov[f"{lvl:.2f}"] = float(((pit >= a / 2) & (pit <= 1 - a / 2)).mean())
    p3 = {"n": len(pit), "pit_mean": float(pit.mean()),
          "pit_values": [round(float(x), 4) for x in pit],
          "central_coverage": cov,
          "note": "coarse: one PIT sample per curated position, small N"}

    return {"n_positions": len(rows), "n_usable": len(usable),
            "n_decisive_excluded": len(rows) - len(usable),
            "P1_spread": p1, "P2_mean_direction": p2, "P3_coverage": p3}


# --- C2 plot ---------------------------------------------------------------- #
def plot_small_multiples(rows, out_png, meta, shared_x=False, xlim=None):
    ordered = sorted(rows, key=lambda r: (CAT_ORDER.index(r["category"]),
                                          -spread80(r)))
    n = len(ordered)
    ncol = 4
    nrow = (n + ncol - 1) // ncol
    fig, axes = plt.subplots(nrow, ncol, figsize=(4.0 * ncol, 2.7 * nrow))
    axes = np.array(axes).reshape(-1)

    L = None
    if shared_x:
        if xlim is None:
            # Robust common half-range: covers the central mass of the widest
            # typical panel without one outlier dominating the scale.
            edges = [max(abs(r["q"]["0.05"]), abs(r["q"]["0.95"])) for r in ordered]
            xlim = float(min(1200.0, np.percentile(edges, 85)))
        L = xlim

    for ax, r in zip(axes, ordered):
        col = CAT_COLOR[r["category"]]
        q = r["q"]
        if shared_x:
            grid = np.linspace(-L, L, 400)
        else:
            lo = min(q["0.01"], r["u"] if not r.get("decisive") else q["0.01"])
            hi = max(q["0.99"], r["u"] if not r.get("decisive") else q["0.99"])
            pad = 0.12 * (hi - lo + 1e-6)
            grid = np.linspace(lo - pad, hi + pad, 400)
        dens = pdf_cp(r, grid)

        ax.fill_between(grid, dens, color=col, alpha=0.22)
        ax.plot(grid, dens, color=col, lw=1.4)
        ax.axvspan(q["0.10"], q["0.90"], color=col, alpha=0.10)  # central 80%
        ax.axvline(0, color=C_ZERO, ls=":", lw=1.0)
        ax.axvline(r["e_u_cp"], color=col, ls="--", lw=1.3)      # E[u|x]
        if not r.get("decisive"):
            ax.axvline(r["u"], color=C_REALIZED, lw=1.6)         # realized u
            pit = cdf_cp(r, r["u"])
            tag = f"PIT={pit:.2f}"
        else:
            tag = "decisive (v* sentinel)"

        corrected = r["v"] - r["e_u_cp"]
        ax.set_title(r["label"], fontsize=9, color=col)
        info = (f"v={r['v']}  v*={r.get('v_star','?')}\n"
                f"E[u]={r['e_u_cp']:.0f}  corr={corrected:.0f}\n{tag}")
        ax.text(0.02, 0.97, info, transform=ax.transAxes, fontsize=7,
                va="top", ha="left",
                bbox=dict(boxstyle="round,pad=0.25", fc="white", ec=col, alpha=0.85))
        ax.set_yticks([])
        ax.set_xlabel("u = v - v*  (cp)", fontsize=7)
        ax.tick_params(labelsize=7)
        if shared_x:
            ax.set_xlim(-L, L)

    for ax in axes[n:]:
        ax.set_visible(False)

    handles = [Patch(color=CAT_COLOR[c], alpha=0.5,
                     label={"tight": "tight (expect narrow)",
                            "wide": "wide (expect broad)",
                            "biased": "biased (expect E[u]!=0)"}[c])
               for c in CAT_ORDER]
    handles += [
        plt.Line2D([0], [0], color=C_ZERO, ls=":", label="u = 0"),
        plt.Line2D([0], [0], color="#444", ls="--", label="E[u|x] (predicted mean)"),
        plt.Line2D([0], [0], color=C_REALIZED, lw=1.6, label="realized u (v - v*)"),
    ]
    fig.legend(handles=handles, loc="lower center", ncol=6, fontsize=8,
               frameon=False, bbox_to_anchor=(0.5, -0.01))
    fig.suptitle(
        f"unc-007 curated p(u|x)  —  net {meta.get('net_run_id','?')}  "
        f"(v* depth {meta.get('vstar_depth','?')})", fontsize=12)
    fig.tight_layout(rect=(0, 0.04, 1, 0.97))
    fig.savefig(out_png, dpi=130, bbox_inches="tight")
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run", default=None, help="a Phase-B run dir (default: latest)")
    ap.add_argument("--root", default=str(DEFAULT_ROOT))
    ap.add_argument("--shared-x", action="store_true",
                    help="use one common cp x-axis on every panel (compare peak widths)")
    ap.add_argument("--xlim", type=float, default=None,
                    help="half-range (cp) for --shared-x (default: robust auto)")
    a = ap.parse_args()

    run_dir = Path(a.run) if a.run else latest_run(a.root)
    rows = load_rows(run_dir)
    meta = json.loads((run_dir / "meta.json").read_text()) if (run_dir / "meta.json").exists() else {}
    print(f"run: {run_dir}  ({len(rows)} positions)")

    figs = run_dir / "figs"
    figs.mkdir(exist_ok=True)
    out_png = figs / ("curated_pux_sharedx.png" if a.shared_x else "curated_pux.png")
    plot_small_multiples(rows, out_png, meta, shared_x=a.shared_x, xlim=a.xlim)

    metrics = compute_metrics(rows)
    (run_dir / "metrics.json").write_text(json.dumps(metrics, indent=2))
    (figs / "plot_metadata.json").write_text(json.dumps(
        {"timestamp": datetime.now().isoformat(), "git_commit": _git_hash(),
         "source_run": str(run_dir)}, indent=2))

    # Console summary.
    p1, p2, p3 = metrics["P1_spread"], metrics["P2_mean_direction"], metrics["P3_coverage"]
    print(f"\nfigure -> {out_png}")
    print(f"metrics -> {run_dir / 'metrics.json'}")
    print(f"\nusable {metrics['n_usable']}/{metrics['n_positions']} "
          f"({metrics['n_decisive_excluded']} decisive excluded)")
    print("P1 spread (80% width, cp):  " +
          "  ".join(f"{c}={p1['width_cp_mean'][c]:.0f}" if p1['width_cp_mean'][c] else f"{c}=-"
                    for c in CAT_ORDER) +
          (f"   wide>tight p={p1['wide_gt_tight_mannwhitney_p']:.3f} "
           f"[{p1.get('verdict','')}]" if 'wide_gt_tight_mannwhitney_p' in p1 else ""))
    if "pearson_r" in p2:
        print(f"P2 E[u] vs u:  pearson r={p2['pearson_r']:.2f} (p={p2['pearson_p']:.3f})  "
              f"spearman r={p2['spearman_r']:.2f}"
              + (f"   sign-agree {p2['sign_agree_rate']:.0%} of {p2['sign_agree_n']}"
                 if 'sign_agree_rate' in p2 else ""))
    print(f"P3 PIT mean={p3['pit_mean']:.2f} (ideal 0.5)  central coverage "
          + " ".join(f"{k}->{v:.2f}" for k, v in p3['central_coverage'].items())
          + f"   (n={p3['n']}, coarse)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
