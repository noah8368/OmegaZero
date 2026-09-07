#!/usr/bin/env python3
"""unc007_sharp_quiet.py — powered P1: predicted spread on sharp vs quiet suites.

The curated small-multiples (unc007_analyze.py) show p(u|x) per named position but
are tiny and hand-labeled. This runs the head over two OBJECTIVELY-labeled public
test suites -- WAC (tactical => sharp) and Silent-but-Deadly (best move not a
capture => quiet) -- and asks the pre-registered P1 question at real N: is the
head's predicted uncertainty (central 80% width of p(u|x)) larger on sharp than on
quiet positions? Fast path only (no v*), so ~435 positions run in one harness pass.

Writes experiment_results/unc-007/sharp_quiet_<ts>/{spread.png, stats.json}.

Usage:
  make unc_harness
  python3 unc_research/scripts/unc007_sharp_quiet.py
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
from scipy.stats import mannwhitneyu

REPO = Path(__file__).resolve().parents[2]
C_SHARP = "#e8820c"
C_QUIET = "#2ca02c"


def epd_to_fens(path):
    """Each EPD line's first 4 fields are the position; append '0 1' for a full FEN."""
    fens = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 4:
            fens.append(" ".join(parts[:4]) + " 0 1")
    return fens


def harness_spreads(harness, net, fens):
    """Run the fast harness; return {fen: central-80%-width (cp)} for each FEN."""
    proc = subprocess.run(
        [harness, net], input="\n".join(fens) + "\n",
        capture_output=True, text=True, check=True)
    out = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        d = json.loads(line)
        if "error" not in d:
            out[d["fen"]] = d["q"]["0.90"] - d["q"]["0.10"]
    return out


def cliffs_delta(a, b):
    """Effect size: P(a>b) - P(a<b); +1 => a strictly larger. O(n log n) via ranks."""
    a = np.asarray(a)
    bs = np.sort(np.asarray(b))
    gt = np.searchsorted(bs, a, side="left").sum()          # count of b < each a
    lt = (len(bs) - np.searchsorted(bs, a, side="right")).sum()  # count of b > each a
    return float(gt - lt) / (len(a) * len(bs))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sharp", default="unc_research/positions/suites/wac_sharp.epd")
    ap.add_argument("--quiet", default="unc_research/positions/suites/sbd_quiet.epd")
    ap.add_argument("--net", default="unc_research/models/nnue_unc.bin")
    ap.add_argument("--harness", default="build/unc_harness")
    ap.add_argument("--out-root", default="unc_research/experiment_results/unc-007")
    a = ap.parse_args()

    sharp_fens = epd_to_fens(a.sharp)
    quiet_fens = epd_to_fens(a.quiet)
    spreads = harness_spreads(a.harness, a.net, sharp_fens + quiet_fens)
    sharp = np.array([spreads[f] for f in sharp_fens if f in spreads])
    quiet = np.array([spreads[f] for f in quiet_fens if f in spreads])
    print(f"sharp (WAC): n={len(sharp)}   quiet (SBD): n={len(quiet)}")

    u, p = mannwhitneyu(sharp, quiet, alternative="greater")
    delta = cliffs_delta(sharp, quiet)
    stats = {
        "n_sharp": int(len(sharp)), "n_quiet": int(len(quiet)),
        "median_width_cp": {"sharp": float(np.median(sharp)),
                            "quiet": float(np.median(quiet))},
        "mean_width_cp": {"sharp": float(sharp.mean()), "quiet": float(quiet.mean())},
        "mannwhitney_u": float(u), "mannwhitney_p_sharp_gt_quiet": float(p),
        "cliffs_delta": float(delta),
        "verdict": ("sharp > quiet (p<1e-3)" if p < 1e-3 else
                    "sharp > quiet (p<0.05)" if p < 0.05 else "no significant ordering"),
    }

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = Path(a.out_root) / f"sharp_quiet_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "stats.json").write_text(json.dumps(stats, indent=2))

    # Figure: overlaid width histograms + median lines.
    fig, ax = plt.subplots(figsize=(8, 4.5))
    hi = np.percentile(np.concatenate([sharp, quiet]), 99)
    bins = np.linspace(0, hi, 40)
    ax.hist(quiet, bins=bins, density=True, color=C_QUIET, alpha=0.55,
            label=f"quiet / SBD (n={len(quiet)}, med {np.median(quiet):.0f}cp)")
    ax.hist(sharp, bins=bins, density=True, color=C_SHARP, alpha=0.55,
            label=f"sharp / WAC (n={len(sharp)}, med {np.median(sharp):.0f}cp)")
    ax.axvline(np.median(quiet), color=C_QUIET, ls="--", lw=2)
    ax.axvline(np.median(sharp), color=C_SHARP, ls="--", lw=2)
    ax.set_xlabel("predicted central 80% width of p(u|x)  (cp)")
    ax.set_ylabel("density")
    ax.set_title(f"unc-007 P1 (powered): sharp vs quiet predicted spread\n"
                 f"Mann-Whitney p={p:.1e}, Cliff's δ={delta:.2f}")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / "spread.png", dpi=130)
    plt.close(fig)

    print(f"\nmedian 80% width:  sharp={np.median(sharp):.0f}cp   quiet={np.median(quiet):.0f}cp")
    print(f"Mann-Whitney (sharp>quiet) p={p:.2e}   Cliff's delta={delta:.2f}")
    print(f"verdict: {stats['verdict']}")
    print(f"figure -> {out_dir / 'spread.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
