#!/usr/bin/env python3
"""unc007_coverage.py — powered P3: calibration/coverage on the val split.

The curated P3 (unc007_analyze.py) is coarse (n~20, off-distribution). This runs
the DEPLOYED head over a large random sample of the validation split -- real
positions that already carry datagen's v_star / u = v - v_star (held out from
training gradients) -- and measures calibration properly: PIT uniformity and
central coverage at N in the thousands. In-distribution, so it's the honest P3.

For each sampled position: run the head (fast harness) -> mixture p(u|x); the PIT
is F(u) = mixture-CDF at the realized u. Calibrated => PIT ~ Uniform(0,1) and
central-C intervals contain u at rate C.

Writes experiment_results/unc-007/coverage_<ts>/{pit.png, stats.json}.

Usage:
  make unc_harness
  python3 unc_research/scripts/unc007_coverage.py --n 5000
"""

import argparse
import json
import random
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.stats import kstest

SCRIPTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_DIR))
from unc007_analyze import cdf_cp  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
C_PIT = "#1f77b4"
C_REF = "#9E9E9E"
DECISIVE = 20000  # drop mate/TB sentinels (u not a smooth error there)


def sample_val(path, n, seed):
    """Random sample of `FEN | v | v_star | u | ...` rows -> [(fen, u), ...]."""
    lines = Path(path).read_text().splitlines()
    rng = random.Random(seed)
    rows = []
    for line in rng.sample(lines, min(n, len(lines))):
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 4:
            continue
        fen, v_star, u = parts[0], int(parts[2]), int(parts[3])
        if abs(v_star) < DECISIVE:
            rows.append((fen, u))
    return rows


def head_mixtures(harness, net, fens):
    proc = subprocess.run([harness, net], input="\n".join(fens) + "\n",
                          capture_output=True, text=True, check=True)
    out = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            d = json.loads(line)
            if "error" not in d:
                out[d["fen"]] = d
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--val", default="nnue/data/unc_11M/validation_data.txt")
    ap.add_argument("--net", default="unc_research/models/nnue_unc.bin")
    ap.add_argument("--harness", default="build/unc_harness")
    ap.add_argument("--n", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out-root", default="unc_research/experiment_results/unc-007")
    a = ap.parse_args()

    rows = sample_val(a.val, a.n, a.seed)
    fens = [f for f, _ in rows]
    print(f"sampled {len(rows)} non-decisive val positions")
    mix = head_mixtures(a.harness, a.net, fens)

    pit = np.array([cdf_cp(mix[f], u) for f, u in rows if f in mix])
    n = len(pit)

    cov = {}
    for lvl in (0.50, 0.80, 0.90, 0.95, 0.99):
        aa = 1 - lvl
        cov[f"{lvl:.2f}"] = float(((pit >= aa / 2) & (pit <= 1 - aa / 2)).mean())
    ks_stat, ks_p = kstest(pit, "uniform")
    stats = {
        "n": int(n), "pit_mean": float(pit.mean()), "pit_std": float(pit.std()),
        "ks_stat": float(ks_stat), "ks_p": float(ks_p),
        "central_coverage": cov,
        "note": "val split (in-distribution, held out from training gradients); "
                "decisive v* sentinels dropped",
    }

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = Path(a.out_root) / f"coverage_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "stats.json").write_text(json.dumps(stats, indent=2))

    # Figure: PIT histogram (flat if calibrated) + PIT-CDF reliability vs diagonal.
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.4))
    ax1.hist(pit, bins=20, range=(0, 1), density=True, color=C_PIT, alpha=0.7)
    ax1.axhline(1.0, color=C_REF, ls="--", lw=1.5, label="uniform (calibrated)")
    ax1.set_xlabel("PIT = F(u | x)")
    ax1.set_ylabel("density")
    ax1.set_title(f"PIT histogram (n={n})\nmean={pit.mean():.3f} (ideal .5), KS={ks_stat:.3f}")
    ax1.legend()

    xs = np.linspace(0, 1, 200)
    emp = np.searchsorted(np.sort(pit), xs, side="right") / n
    ax2.plot(xs, emp, color=C_PIT, lw=2, label="empirical PIT CDF")
    ax2.plot([0, 1], [0, 1], color=C_REF, ls="--", lw=1.5, label="ideal")
    ax2.set_xlabel("nominal level")
    ax2.set_ylabel("empirical fraction with PIT <= level")
    ax2.set_title("reliability (PIT CDF vs diagonal)")
    ax2.legend()
    fig.suptitle("unc-007 P3 (powered): head calibration on the val split", y=1.02)
    fig.tight_layout()
    fig.savefig(out_dir / "pit.png", dpi=130, bbox_inches="tight")
    plt.close(fig)

    print(f"\nPIT mean={pit.mean():.3f} (ideal 0.5)  KS={ks_stat:.3f} (p={ks_p:.1e})")
    print("central coverage (nominal -> empirical):")
    for k, v in cov.items():
        print(f"  {k} -> {v:.3f}")
    print(f"figure -> {out_dir / 'pit.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
