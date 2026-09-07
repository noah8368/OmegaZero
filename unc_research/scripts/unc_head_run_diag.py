#!/usr/bin/env python3
"""Diagnostic for the 2026-09-05 unc-head run: shows that its training .bin was
98% zero-filled (an interrupted encode the mtime-only cache silently accepted),
which invalidates the run as a 10.6M read and poisons the unconditional floor.

Usage: unc_head_run_diag.py <train.bin> <val.bin> <run_dir> <out.png>
"""
import sys
from pathlib import Path

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))  # prepare_unc_data is a sibling
from prepare_unc_data import UNC_RECORD_DTYPE  # noqa: E402

TRAIN_BIN, VAL_BIN, RUN_DIR, OUT = sys.argv[1:5]

import json  # noqa: E402

tr = np.fromfile(TRAIN_BIN, dtype=UNC_RECORD_DTYPE)
va = np.fromfile(VAL_BIN, dtype=UNC_RECORD_DTYPE)
art = np.load(Path(RUN_DIR) / "artifacts.npz")
met = json.load(open(Path(RUN_DIR) / "metrics.json"))

tr_real = tr["depth"] == 12
n_tr, n_real = len(tr), int(tr_real.sum())
va_real = int((va["depth"] == 12).sum())
u_real = tr["u"][tr_real].astype(np.float64)
u_all = tr["u"].astype(np.float64)

print(f"train.bin : {n_tr:,} records, {n_real:,} real ({n_real/n_tr*100:.2f}%), "
      f"{n_tr-n_real:,} zero-filled")
print(f"val.bin   : {len(va):,} records, {va_real:,} real")
print(f"u (real)  : mean={u_real.mean():.1f} std={u_real.std():.1f}")
print(f"u (all, what standardize saw): mean={u_all.mean():.2f} std={u_all.std():.2f}")

plt.rcParams.update({"figure.dpi": 120, "font.size": 9,
                     "axes.grid": True, "grid.alpha": 0.25})
fig, ax = plt.subplots(2, 2, figsize=(12, 8))
C, C2, C3, Cref = "#4C78A8", "#E45756", "#54A24B", "#888888"

# A: validity vs record index (real block then zeros).
step = max(1, n_tr // 4000)
idx = np.arange(0, n_tr, step)
ax[0, 0].fill_between(idx, tr_real[::step].astype(float), color=C, step="mid")
ax[0, 0].set(title=f"train.bin record validity — only {n_real:,} / {n_tr:,} real "
             f"({n_real/n_tr*100:.1f}%)",
             xlabel="record index", ylabel="depth==12 (real)", ylim=(-0.05, 1.15))
ax[0, 0].axvline(n_real, color=C2, lw=1.2, ls="--")
ax[0, 0].text(n_real, 1.05, f"  encode killed here ({n_real:,})",
              color=C2, fontsize=8, va="top")

# B: u distribution — real vs all-records (the spike at 0 the zeros inject).
bins = np.linspace(-600, 600, 120)
ax[0, 1].hist(u_all, bins=bins, density=True, color=Cref, alpha=0.6,
              label=f"all records (std {u_all.std():.0f})  <- standardized on this")
ax[0, 1].hist(u_real, bins=bins, density=True, color=C, alpha=0.6,
              label=f"real records (std {u_real.std():.0f})")
ax[0, 1].set(title="target u: 98% zeros crush the standardization scale",
             xlabel="u = v - v*  (cp)", ylabel="density", xlim=(-600, 600))
ax[0, 1].legend(fontsize=8)

# C: coverage — the poisoned unconditional floor collapses to ~0.5% everywhere.
lv = np.asarray(met["coverage_nominal"], dtype=float)
lv = lv * 100 if lv.max() <= 1.0 else lv  # accept fractions or percents
cc = np.asarray(art["cond_coverage"], dtype=float)
uc = np.asarray(art["unc_coverage"], dtype=float)
ax[1, 0].plot([0, 100], [0, 100], color=Cref, ls="--", label="ideal")
ax[1, 0].plot(lv, cc * 100, "o-", color=C, label="conditional (trained on 174k real)")
ax[1, 0].plot(lv, uc * 100, "s-", color=C2, label="uncond floor (near-delta, poisoned)")
ax[1, 0].set(title="coverage: floor collapsed -> 'gain' is vs a broken baseline",
             xlabel="nominal central-interval (%)", ylabel="empirical coverage (%)",
             xlim=(40, 100), ylim=(-3, 100))
ax[1, 0].legend(fontsize=8)

# D: the punchline — effective train set is smaller than the val set.
labels = ["train\n(real used)", "train\n(zero-filled)", "val\n(intact)"]
vals = [n_real, n_tr - n_real, va_real]
colors = [C, C2, C3]
bars = ax[1, 1].bar(labels, vals, color=colors, alpha=0.8)
ax[1, 1].set(title="effective data: real-train < val (should be ~9:1 the other way)",
             ylabel="records")
ax[1, 1].set_yscale("log")
for b, val in zip(bars, vals):
    ax[1, 1].text(b.get_x() + b.get_width() / 2, val, f"{val:,}",
                  ha="center", va="bottom", fontsize=8)

fig.suptitle("unc-head run 2026-09-05 — INVALID: training .bin 98% zero-filled",
             fontsize=13, y=0.995)
fig.tight_layout(rect=[0, 0, 1, 0.97])
fig.savefig(OUT, bbox_inches="tight")
print(f"\nwrote {OUT}")
