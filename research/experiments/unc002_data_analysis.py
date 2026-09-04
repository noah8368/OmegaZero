#!/usr/bin/env python3
"""Analyze the uncertainty datagen run: distributions of v, v*, residual, and
how disagreement (|v - v*|) scales with eval magnitude, search effort, phase,
and result. Emits a multi-panel PNG + printed summary stats.

Row format (pipe-separated): FEN | v | v* | (v - v*) | depth | nodes | result
  v   = static Evaluate (cheap estimate)
  v*  = fixed depth-12 search score (the "truth" target)
  r   = v - v*  (static minus search; +ve => static over-estimates vs search)
"""
import glob
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RUN = sys.argv[1]
OUT = sys.argv[2]

fens_stm, v, vstar, nodes, result, npieces = [], [], [], [], [], []
n_bad = 0
for path in sorted(glob.glob(f"{RUN}/*.txt")):
    with open(path) as fh:
        for line in fh:
            parts = line.split("|")
            if len(parts) != 7:
                n_bad += 1
                continue
            try:
                fen = parts[0].strip()
                vi = int(parts[1])
                vsi = int(parts[2])
                nd = int(parts[5])
                res = float(parts[6])
            except ValueError:
                n_bad += 1
                continue
            board = fen.split(" ", 1)[0]
            # piece count = non-digit, non-slash chars in the placement field
            npc = sum(ch.isalpha() for ch in board)
            fens_stm.append(fen.split(" ")[1])
            v.append(vi)
            vstar.append(vsi)
            nodes.append(nd)
            result.append(res)
            npieces.append(npc)

v = np.array(v, dtype=np.float64)
vstar = np.array(vstar, dtype=np.float64)
nodes = np.array(nodes, dtype=np.float64)
result = np.array(result)
npieces = np.array(npieces)
r = v - vstar          # residual (static - search)
absr = np.abs(r)
n = len(v)

def pct(a, p):
    return np.percentile(a, p)

print(f"clean rows: {n:,}   dropped(malformed): {n_bad}")
print("\n=== v (static Evaluate), cp ===")
print(f"  mean={v.mean():.1f}  std={v.std():.1f}  "
      f"p1={pct(v,1):.0f} p50={pct(v,50):.0f} p99={pct(v,99):.0f}")
print("=== v* (depth-12 search), cp ===")
print(f"  mean={vstar.mean():.1f}  std={vstar.std():.1f}  "
      f"p1={pct(vstar,1):.0f} p50={pct(vstar,50):.0f} p99={pct(vstar,99):.0f}")
print("=== residual r = v - v*, cp ===")
print(f"  mean={r.mean():.2f}  median={np.median(r):.1f}  std={r.std():.1f}")
print(f"  mean|r|={absr.mean():.1f}  median|r|={np.median(absr):.1f}  "
      f"p90|r|={pct(absr,90):.0f}  p99|r|={pct(absr,99):.0f}  max|r|={absr.max():.0f}")
# excess kurtosis of r (heavy-tail check for flow base dist choice)
rn = (r - r.mean()) / r.std()
print(f"  skew={ (rn**3).mean():.2f}  excess_kurtosis={ (rn**4).mean()-3:.1f}  "
      f"(0 => Gaussian tails)")
# correlation of |r| with predictors
print("\n=== Spearman-ish signal (Pearson on ranks) of |r| vs predictors ===")
def rank_corr(x, y):
    xr = np.argsort(np.argsort(x)); yr = np.argsort(np.argsort(y))
    return np.corrcoef(xr, yr)[0, 1]
print(f"  |r| vs |v|      : {rank_corr(absr, np.abs(v)):+.3f}")
print(f"  |r| vs nodes    : {rank_corr(absr, nodes):+.3f}")
print(f"  |r| vs #pieces  : {rank_corr(absr, npieces):+.3f}")

# ---------------- plots ----------------
plt.rcParams.update({"figure.dpi": 120, "font.size": 9,
                     "axes.grid": True, "grid.alpha": 0.25})
fig, ax = plt.subplots(2, 3, figsize=(15, 8.5))
C = "#4C78A8"; C2 = "#E45756"; C3 = "#54A24B"

# 1: v vs v* joint (2D hist) — how far search moves the static eval
clip = 600
m = (np.abs(v) < clip) & (np.abs(vstar) < clip)
h = ax[0, 0].hist2d(v[m], vstar[m], bins=120, cmap="magma",
                    norm=matplotlib.colors.LogNorm())
ax[0, 0].plot([-clip, clip], [-clip, clip], "w--", lw=0.8, alpha=0.7)
ax[0, 0].set(title="v* (depth-12) vs v (static)", xlabel="v  (cp)",
             ylabel="v*  (cp)", xlim=(-clip, clip), ylim=(-clip, clip))
fig.colorbar(h[3], ax=ax[0, 0], label="count (log)")

# 2: residual distribution
rc = np.clip(r, -400, 400)
ax[0, 1].hist(rc, bins=160, color=C, alpha=0.9)
ax[0, 1].axvline(0, color="k", lw=0.8)
ax[0, 1].axvline(r.mean(), color=C2, lw=1.2, ls="--",
                 label=f"mean={r.mean():.1f}")
ax[0, 1].set(title="residual  r = v - v*  (clipped ±400)",
             xlabel="cp", ylabel="count", yscale="log")
ax[0, 1].legend()

# 3: |r| vs eval magnitude (binned mean + p90)
def binned(x, y, edges):
    idx = np.digitize(x, edges)
    cx, my, p9 = [], [], []
    for b in range(1, len(edges)):
        sel = idx == b
        if sel.sum() < 50:
            continue
        cx.append(0.5 * (edges[b - 1] + edges[b]))
        my.append(y[sel].mean()); p9.append(np.percentile(y[sel], 90))
    return np.array(cx), np.array(my), np.array(p9)

e = np.linspace(0, 800, 25)
cx, my, p9 = binned(np.abs(v), absr, e)
ax[0, 2].plot(cx, my, "-o", color=C, ms=3, label="mean |r|")
ax[0, 2].plot(cx, p9, "-o", color=C2, ms=3, label="p90 |r|")
ax[0, 2].set(title="uncertainty grows with |eval|", xlabel="|v|  (cp)",
             ylabel="|r|  (cp)")
ax[0, 2].legend()

# 4: |r| vs search nodes (difficulty proxy), log-x
ne = np.logspace(np.log10(max(nodes.min(), 1)), np.log10(nodes.max()), 25)
cx, my, p9 = binned(nodes, absr, ne)
ax[1, 0].plot(cx, my, "-o", color=C, ms=3, label="mean |r|")
ax[1, 0].plot(cx, p9, "-o", color=C2, ms=3, label="p90 |r|")
ax[1, 0].set(title="uncertainty vs search effort", xlabel="nodes (depth-12)",
             ylabel="|r|  (cp)", xscale="log")
ax[1, 0].legend()

# 5: |r| vs game phase (#pieces)
pe = np.arange(2, 33) - 0.5
cx, my, p9 = binned(npieces, absr, pe)
ax[1, 1].plot(cx, my, "-o", color=C, ms=3, label="mean |r|")
ax[1, 1].plot(cx, p9, "-o", color=C2, ms=3, label="p90 |r|")
ax[1, 1].set(title="uncertainty vs game phase", xlabel="# pieces on board",
             ylabel="|r|  (cp)")
ax[1, 1].invert_xaxis()
ax[1, 1].legend()

# 6: residual by game result
labels = [("loss", 0.0), ("draw", 0.5), ("win", 1.0)]
data = [r[result == val] for _, val in labels]
parts = ax[1, 2].violinplot(data, showmeans=True, showextrema=False)
for pc in parts["bodies"]:
    pc.set_facecolor(C3); pc.set_alpha(0.6)
ax[1, 2].set(title="residual by game result (STM POV)",
             xticks=[1, 2, 3], xticklabels=[f"{l}\n(n={len(d):,})"
             for (l, _), d in zip(labels, data)],
             ylabel="r = v - v*  (cp)", ylim=(-300, 300))
ax[1, 2].axhline(0, color="k", lw=0.8)

fig.suptitle(f"Uncertainty datagen — {RUN.split('/')[-1]}   "
             f"(n={n:,}, depth-12 v*)", fontsize=12, y=0.995)
fig.tight_layout(rect=[0, 0, 1, 0.98])
fig.savefig(OUT, bbox_inches="tight")
print(f"\nwrote {OUT}")
