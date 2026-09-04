#!/usr/bin/env python3
"""Conditional (bucketed) shape check on u = v - v*.

We can't condition on the NNUE embedding x (not wired yet), so we bucket on
observable proxies — game phase (# pieces) x eval magnitude (|v|) — and ask, per
bucket: is p(u | bucket) still unimodal + heavy-tailed, or does conditioning
reveal bimodality (the flow's only home-turf advantage over mdn_t)?

Bimodality coefficient BC = (skew^2 + 1) / (excess_kurt + 3) (large-n form).
Uniform => 0.555; BC > 0.555 hints bimodal/light-tailed; BC < 0.555 => unimodal.
Heavy tails push BC down, so we ALSO eyeball each bucket histogram.
"""
import glob
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RUN, OUT = sys.argv[1], sys.argv[2]
v, u, npieces = [], [], []
for path in sorted(glob.glob(f"{RUN}/*.txt")):
    with open(path) as fh:
        for line in fh:
            p = line.split("|")
            if len(p) != 7:
                continue
            try:
                vi, vsi = int(p[1]), int(p[2])
            except ValueError:
                continue
            board = p[0].split(" ", 1)[0]
            v.append(vi)
            u.append(vi - vsi)
            npieces.append(sum(c.isalpha() for c in board))
v = np.array(v, float); u = np.array(u, float); npieces = np.array(npieces)
av = np.abs(v)

phase_bins = [("endgame\n(<12 pc)", npieces < 12),
              ("middlegame\n(12-24 pc)", (npieces >= 12) & (npieces <= 24)),
              ("opening\n(>24 pc)", npieces > 24)]
eval_bins = [("quiet |v|<50", av < 50),
             ("moderate 50-300", (av >= 50) & (av < 300)),
             ("decisive |v|>300", av >= 300)]

def shape(x):
    n = len(x)
    m, s = x.mean(), x.std()
    z = (x - m) / s
    sk = (z ** 3).mean()
    ek = (z ** 4).mean() - 3
    bc = (sk ** 2 + 1) / (ek + 3)
    return n, m, s, sk, ek, bc

print(f"{'bucket':<34}{'n':>9}{'mean':>8}{'std':>8}"
      f"{'skew':>7}{'ex.kurt':>9}{'BC':>7}  flag")
fig, axes = plt.subplots(3, 3, figsize=(13, 10), sharex=True)
for i, (pl, pm) in enumerate(phase_bins):
    for j, (el, em) in enumerate(eval_bins):
        sel = pm & em
        x = u[sel]
        ax = axes[i, j]
        if len(x) < 200:
            ax.set_title(f"{pl.splitlines()[0]} / {el}\n(n={len(x)})",
                         fontsize=8)
            ax.text(0.5, 0.5, "too few", ha="center", va="center",
                    transform=ax.transAxes)
            continue
        n, m, s, sk, ek, bc = shape(x)
        flag = "BIMODAL?" if bc > 0.555 else ""
        name = f"{pl.splitlines()[0]}/{el}"
        print(f"{name:<34}{n:>9,}{m:>8.1f}{s:>8.1f}"
              f"{sk:>7.2f}{ek:>9.1f}{bc:>7.3f}  {flag}")
        ax.hist(np.clip(x, -400, 400), bins=80, color="#4C78A8", alpha=0.9)
        ax.set_yscale("log")
        ax.axvline(0, color="k", lw=0.6)
        ax.axvline(m, color="#E45756", lw=1.0, ls="--")
        ax.set_title(f"{name}\nn={n:,}  ex.kurt={ek:.1f}  BC={bc:.3f}"
                     + ("  ⚠BIMODAL?" if flag else ""), fontsize=8)
for ax in axes[-1]:
    ax.set_xlabel("u = v - v*  (cp, clipped ±400)")
fig.suptitle("Conditional shape of u by phase x eval-magnitude bucket "
             "(log-y). Unimodal + heavy-tailed everywhere => mdn_t regime.",
             fontsize=11)
fig.tight_layout(rect=[0, 0, 1, 0.97])
fig.savefig(OUT, bbox_inches="tight")
print(f"\nBC>0.555 in any bucket would hint conditional bimodality (flow's edge).")
print(f"wrote {OUT}")
