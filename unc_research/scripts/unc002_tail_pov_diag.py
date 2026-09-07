#!/usr/bin/env python3
"""unc-002 follow-up diagnostics on the residual r = v - v* of an uncertainty
datagen run. Answers two questions the six-panel overview
(`unc002_data_analysis.py`) left open:

  (A) TAILS — how heavy are they, quantitatively? Reports tail mass, excess
      kurtosis, a method-of-moments Student-t nu, and a Hill tail-index for each
      side, then overlays the empirical survival of |r| against fitted Gaussian /
      Laplace / Student-t curves. This is the evidence for the Student-t head +
      nu-floor choice (mdn_t) over a Gaussian MDN.

  (B) POV — the datagen writes `result` WHITE POV (datagen.cc ResultToStr:
      kWhiteWin->1.0 / kBlackWin->0.0), applied to EVERY sampled position of a
      game, while v and v* are STM POV. So the overview's "residual by game
      result (STM POV)" violin actually groups an STM-POV residual by a White-POV
      label -> POVs are mixed for every Black-to-move position. This script
      recomputes a proper STM-POV result (flip when Black to move), compares the
      win/loss residual skew under both groupings, and breaks the decisive-
      position win:loss gap down by side-to-move and by ply to confirm it is the
      expected White first-move advantage, not a sampling artifact.

Row format (pipe-separated): FEN | v | v* | (v - v*) | depth | nodes | result
  v      = static Evaluate (STM POV)
  v*     = fixed depth-12 search score (STM POV)
  result = game outcome, WHITE POV (1.0 / 0.5 / 0.0), same for all plies of a game

Usage: unc002_tail_pov_diag.py <RUN_dir_or_glob> <out.png>
"""
import glob
import sys

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from scipy import stats  # noqa: E402

RUN = sys.argv[1]
OUT = sys.argv[2]

# ---------------- load ----------------
# NB: the datagen FEN hardcodes the fullmove counter to 1 (only the halfmove
# clock varies), so game ply / move-number is NOT recoverable from a row -- a
# metadata gap in the unc-002 schema (fen|v|v*|u|depth|nodes|result). We rely on
# the side-to-move balance instead to rule out a sampling-side bias.
v, vstar, result, is_white = [], [], [], []
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
                res = float(parts[6])
            except ValueError:
                n_bad += 1
                continue
            stm = fen.split()[1]
            v.append(vi)
            vstar.append(vsi)
            result.append(res)
            is_white.append(stm == "w")

v = np.array(v, dtype=np.float64)
vstar = np.array(vstar, dtype=np.float64)
result = np.array(result, dtype=np.float64)
is_white = np.array(is_white, dtype=bool)
r = v - vstar
absr = np.abs(r)
n = len(v)
m = r.mean()
s = r.std()

# STM-POV result: White-POV result is correct when White is to move; flip
# (1 - result) when Black is to move so win/draw/loss are read from the STM.
result_stm = np.where(is_white, result, 1.0 - result)


def sf_emp(x, thresh):
    return float((x > thresh).mean())


def hill(tail_vals, ks):
    """Hill tail-index alpha over the top-k order statistics, for a range of k.
    tail_vals: positive exceedances (already > 0). Returns alpha_hat per k."""
    xs = np.sort(tail_vals)[::-1]  # descending
    out = []
    for k in ks:
        if k + 1 >= len(xs) or xs[k] <= 0:
            out.append(np.nan)
            continue
        top = xs[:k]
        top = top[top > 0]
        # alpha = 1 / mean(log(x_i / x_k))
        denom = np.mean(np.log(top / xs[k]))
        out.append(1.0 / denom if denom > 0 else np.nan)
    return np.array(out)


# ---------------- (A) tails ----------------
skew = stats.skew(r)
exkurt = stats.kurtosis(r, fisher=True)  # excess (0 => Gaussian)
# Method-of-moments Student-t: excess kurtosis = 6/(nu-4) for nu>4 => nu=4+6/k.
nu_mom = 4.0 + 6.0 / exkurt if exkurt > 0 else np.inf

# Hill on each side, centered at the mean.
rc = r - m
right = rc[rc > 0]
left = -rc[rc < 0]
ks = np.unique(np.linspace(50, min(len(right), len(left)) // 20, 40).astype(int))
ks = ks[ks >= 50]
alpha_r = hill(right, ks)
alpha_l = hill(left, ks)
# a stable point estimate: median over the middle band of k
band = (ks > ks.max() * 0.1) & (ks < ks.max() * 0.5)
alpha_r_pt = np.nanmedian(alpha_r[band])
alpha_l_pt = np.nanmedian(alpha_l[band])

print(f"clean rows: {n:,}   dropped(malformed): {n_bad}")
print("\n=== (A) residual r = v - v*  (cp), UNCLIPPED ===")
print(f"  mean={m:.2f}  std={s:.1f}  skew={skew:+.2f}  excess_kurtosis={exkurt:.1f}")
print(f"  method-of-moments Student-t nu (from kurtosis) ~ {nu_mom:.2f}"
      "   (nu->inf = Gaussian; lower = heavier)")
print(f"  Hill tail index alpha:  right={alpha_r_pt:.2f}  left={alpha_l_pt:.2f}"
      "   (alpha ~ nu for a Student-t; lower = heavier / more skew if L!=R)")
print("  tail mass  P(|r| > x):")
for x in (100, 200, 300, 400, 600, 800, 1000, 1500):
    print(f"    |r|>{x:>4}cp : {sf_emp(absr, x)*100:6.3f}%   "
          f"(one-sided  r>+{x}: {sf_emp(r, x)*100:5.3f}%   "
          f"r<-{x}: {sf_emp(-r, x)*100:5.3f}%)")
print(f"  max|r| = {absr.max():.0f}cp")

# ---------------- (B) POV ----------------
n_wwin = int((result == 1.0).sum())
n_bwin = int((result == 0.0).sum())
n_draw = int((result == 0.5).sum())
print("\n=== (B) result POV ===")
print("  result is WHITE-POV in the data (same label on every ply of a game).")
print(f"  white-win positions: {n_wwin:,} ({n_wwin/n*100:.1f}%)")
print(f"  black-win positions: {n_bwin:,} ({n_bwin/n*100:.1f}%)")
print(f"  draw positions     : {n_draw:,} ({n_draw/n*100:.1f}%)")
dec = n_wwin + n_bwin
print(f"  decisive white:black = {n_wwin/max(n_bwin,1):.2f} : 1 "
      f"(white share of decisive = {n_wwin/max(dec,1)*100:.1f}%)")

# Is the gap a sampling artifact? Split decisive positions by side-to-move.
white_stm = is_white
print("  side-to-move balance (should be ~50/50 if sampling is unbiased):")
print(f"    STM=white: {white_stm.mean()*100:.1f}%   STM=black: {(~white_stm).mean()*100:.1f}%")
for lab, sel in (("white-win", result == 1.0), ("black-win", result == 0.0)):
    ww = is_white[sel]
    print(f"    within {lab}: STM=white {ww.mean()*100:.1f}%  STM=black {(1-ww.mean())*100:.1f}%")

# Skew of r within win/loss groups, mislabeled (White-POV) vs corrected (STM-POV).
def grp_stats(res_arr, val):
    g = r[res_arr == val]
    return g.mean(), stats.skew(g), np.percentile(g, 5), np.percentile(g, 95)


print("\n  residual r within win/loss groups:")
print("             grouping     mean     skew      p5      p95")
for name, res_arr in (("WHITE-POV (mislabeled)", result),
                      ("STM-POV  (corrected)  ", result_stm)):
    for lab, val in (("win ", 1.0), ("loss", 0.0)):
        mn, sk, p5, p95 = grp_stats(res_arr, val)
        print(f"    {name}  {lab}  {mn:7.1f}  {sk:+6.2f}  {p5:7.0f}  {p95:7.0f}")

# ---------------- plots ----------------
plt.rcParams.update({"figure.dpi": 120, "font.size": 9,
                     "axes.grid": True, "grid.alpha": 0.25})
fig, ax = plt.subplots(2, 3, figsize=(15, 8.5))
C = "#4C78A8"
C2 = "#E45756"
C3 = "#54A24B"
C4 = "#B279A2"

# A1: empirical survival of |r - mean| vs Gaussian / Laplace / Student-t fits.
xs = np.linspace(0, min(1500, absr.max()), 400)
arc = np.abs(rc)
emp = np.array([sf_emp(arc, x) for x in xs])
# fitted params (centered): Gaussian std s; Laplace b = mean|.|; Student-t nu_mom
b_lap = arc.mean()
nu_fit = max(nu_mom, 2.1)
sigma_t = s * np.sqrt(max(nu_fit - 2.0, 1e-3) / nu_fit)
g_sf = 2 * stats.norm.sf(xs / s)
l_sf = np.exp(-xs / b_lap)
t_sf = 2 * stats.t.sf(xs / sigma_t, df=nu_fit)
ax[0, 0].semilogy(xs, emp, color="k", lw=1.6, label="empirical |r-mean|")
ax[0, 0].semilogy(xs, g_sf, color=C, ls="--", label="Gaussian fit")
ax[0, 0].semilogy(xs, l_sf, color=C3, ls="--", label="Laplace fit")
ax[0, 0].semilogy(xs, t_sf, color=C2, ls="--",
                  label=f"Student-t fit (nu={nu_fit:.1f})")
ax[0, 0].set(title="tail survival: Student-t vs Gaussian/Laplace",
             xlabel="|r - mean|  (cp)", ylabel="P(|r-mean| > x)",
             ylim=(max(emp[emp > 0].min(), 1e-6), 1.2))
ax[0, 0].legend(fontsize=8)

# A2: Hill tail-index vs k (left/right tails).
ax[0, 1].plot(ks, alpha_r, "-o", color=C2, ms=2, label="right tail (r>0)")
ax[0, 1].plot(ks, alpha_l, "-o", color=C, ms=2, label="left tail (r<0)")
ax[0, 1].axhline(alpha_r_pt, color=C2, ls=":", lw=1)
ax[0, 1].axhline(alpha_l_pt, color=C, ls=":", lw=1)
ax[0, 1].set(title="Hill tail index alpha (~ Student-t nu)",
             xlabel="k (top order statistics)", ylabel="alpha_hat",
             ylim=(0, max(np.nanmax(alpha_r), np.nanmax(alpha_l)) * 1.2))
ax[0, 1].legend(fontsize=8)

# A3: QQ of standardized r vs fitted Student-t (tail-fit quality).
qs = np.linspace(0.001, 0.999, 400)
samp_q = np.quantile((r - m) / sigma_t, qs)
theo_q = stats.t.ppf(qs, df=nu_fit)
ax[0, 2].plot(theo_q, samp_q, color=C2, lw=1.4)
lim = np.nanpercentile(np.abs(np.concatenate([theo_q, samp_q])), 99.5)
ax[0, 2].plot([-lim, lim], [-lim, lim], "k--", lw=0.8, alpha=0.7)
ax[0, 2].set(title=f"QQ: standardized r vs Student-t(nu={nu_fit:.1f})",
             xlabel="theoretical quantile", ylabel="sample quantile",
             xlim=(-lim, lim), ylim=(-lim, lim))

# B1: residual violins — White-POV (mislabeled) vs STM-POV (corrected).
def violins(axis, res_arr, title):
    labels = [("loss", 0.0), ("draw", 0.5), ("win", 1.0)]
    data = [r[res_arr == val] for _, val in labels]
    parts = axis.violinplot(data, showmeans=True, showextrema=False)
    for pc in parts["bodies"]:
        pc.set_facecolor(C4)
        pc.set_alpha(0.6)
    axis.set(title=title, xticks=[1, 2, 3],
             xticklabels=[f"{lab}\n(n={len(d):,})"
                          for (lab, _), d in zip(labels, data)],
             ylabel="r = v - v*  (cp)", ylim=(-300, 300))
    axis.axhline(0, color="k", lw=0.8)


violins(ax[1, 0], result, "by result — WHITE-POV (as plotted before: MIXED POV)")
violins(ax[1, 1], result_stm, "by result — STM-POV (corrected)")

# B2: the signal the POV bug hid — STM-POV win vs loss residual densities.
# When the side-to-move eventually WINS, r skews negative (static under-rates
# what search sees); when it LOSES, r skews positive (static over-rates).
r_win = r[result_stm == 1.0]
r_loss = r[result_stm == 0.0]
bins = np.linspace(-500, 500, 120)
ax[1, 2].hist(r_loss, bins=bins, density=True, color=C2, alpha=0.55,
              label=f"STM loses (mean {r_loss.mean():+.0f})")
ax[1, 2].hist(r_win, bins=bins, density=True, color=C, alpha=0.55,
              label=f"STM wins (mean {r_win.mean():+.0f})")
ax[1, 2].axvline(0, color="k", lw=0.8)
ax[1, 2].set(title="POV-corrected: r | STM result (loss vs win)",
             xlabel="r = v - v*  (cp)", ylabel="density", xlim=(-500, 500))
ax[1, 2].legend(fontsize=8)

fig.suptitle(f"unc-002 tail + POV diagnostics — {RUN.rstrip('/').split('/')[-1]}   "
             f"(n={n:,})", fontsize=12, y=0.995)
fig.tight_layout(rect=[0, 0, 1, 0.98])
fig.savefig(OUT, bbox_inches="tight")
print(f"\nwrote {OUT}")
