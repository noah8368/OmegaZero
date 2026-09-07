# unc-002 follow-up: tail characterization + a POV bug in the result label

- **Status:** analysis complete (2026-09-05)
- **Dataset:** `nnue/data/unc_11M/` (11,808,289 clean rows; 1 malformed dropped), depth-12 `v*`
- **Code:** `unc_research/scripts/unc002_tail_pov_diag.py`
- **Figure:** `unc_research/experiment_results/unc-002/tail_pov_diag_unc11M.png`
- **Follows:** `unc002_data_analysis.py` (the six-panel overview) — this answers the two
  questions that overview left open: *how heavy are the tails, quantitatively?* and *is the
  win/loss residual asymmetry real?*

Run:

```
.venv/bin/python unc_research/scripts/unc002_tail_pov_diag.py \
    nnue/data/unc_11M \
    unc_research/experiment_results/unc-002/tail_pov_diag_unc11M.png
```

---

## (A) Tails — quantitative support for the Student-t head + ν-floor

`r = v − v*`, unclipped, over all 11.8M rows:

| stat | value |
|---|---|
| mean | **+13.1 cp** (static mildly over-rates vs search) |
| std | 193.9 cp |
| skew | **+0.35** (right tail slightly fatter) |
| excess kurtosis | **19.4** (Gaussian = 0) |
| method-of-moments Student-t ν (from kurtosis) | **≈ 4.31** |
| Hill tail index α — right / left | **3.83 / 4.02** (α ≈ ν for a Student-t) |

**Tail mass** `P(|r| > x)`:

| x (cp) | P(\|r\|>x) | r > +x | r < −x |
|---:|---:|---:|---:|
| 100 | 37.1% | 21.0% | 16.1% |
| 200 | 17.1% | 9.7% | 7.4% |
| 300 | 9.2% | 5.1% | 4.0% |
| 400 | 5.4% | 3.0% | 2.4% |
| 600 | 2.05% | 1.15% | 0.90% |
| 800 | 0.84% | 0.48% | 0.36% |
| 1000 | 0.34% | 0.21% | 0.14% |
| 1500 | 0.030% | 0.020% | 0.010% |

`max|r| = 19,427 cp` — a lone extreme outlier (P(|r|>1500) is only 0.03%); almost certainly a
near-sentinel `v*` that squeaked under `kDecisiveScoreThreshold`. Worth a spot-check but
immaterial to the fit (see open items).

The `|r|≥600 = 2.05%` figure **matches the finalization note's `|u|≥600 = 2.1%`** — independent
confirmation the loader agrees with the combine step.

**Reading (fig, top row):**
- **Tail-survival panel:** empirical `P(|r−mean| > x)` sits *far* above the Gaussian fit (which
  collapses by ~400 cp), above the Laplace fit, and tracks the **Student-t(ν≈4.3)** curve closely
  out to ~1200 cp. This is the direct visual case for a Student-t density over a Gaussian MDN.
- **Hill panel:** both tails plateau near **α ≈ 3.8–4.0** in the stable middle-k band (the small-k
  spike and large-k downward drift are the usual Hill artifacts, not signal). Right slightly
  heavier than left, consistent with the +0.35 skew.
- **QQ panel:** standardized `r` vs Student-t(ν=4.3) rides the diagonal through the body and only
  bows out past ±3–4 σ — the observed tail is *marginally lighter* than the fitted t in the far
  tail, i.e. **finite**, whereas a ν-floor of 2.0 permits infinite variance. So the planned
  **ν-floor 2.0 is non-binding** (headroom below the observed ~4), exactly as the research_log
  claimed. Good.

**Bottom line:** heavy-tailed (ν≈4, kurtosis 19), mildly right-skewed, tail finite but well
past Gaussian. Vindicates `mdn_t` (Student-t mixture) over a Gaussian MDN, and the ν-floor choice.

## (B) POV bug — `result` is White-POV, but `v`/`v*` are STM-POV

**Finding.** `datagen.cc` writes `result` via `ResultToStr` (`kWhiteWin→1.0 / kBlackWin→0.0 /
kDrawResult→0.5`) and stamps that **same White-POV label on every sampled position of the game**
(`datagen.cc:660–664`). But `v` and `v*` are **STM-POV** (`datagen.cc:367,380`). So the overview's
sixth panel — titled *"residual by game result (STM POV)"* — actually groups an **STM-POV residual
by a White-POV label**: for every Black-to-move position the two POVs disagree, scrambling the
sign relationship between residual and outcome.

**Impact — the mislabeling washes the signal out (fig, bottom-left vs -middle):**

| grouping | group | mean r | skew | p5 | p95 |
|---|---|---:|---:|---:|---:|
| **White-POV (mislabeled)** | win | +13.4 | +0.31 | −297 | +328 |
| | loss | +11.2 | +0.24 | −310 | +344 |
| **STM-POV (corrected)** | win | **−64.6** | **−1.63** | −410 | +155 |
| | loss | **+88.9** | **+1.95** | −133 | +445 |

Under the (buggy) White-POV grouping, win and loss are **nearly identical** — the earlier "wins
have a fat negative tail" read was an eyeball artifact; the mixing had actually flattened it.
Under the **corrected STM-POV** grouping a large, correctly-signed effect appears: when the side
to move eventually **wins**, `r` skews **negative** (`v < v*` → static *under*-rates what search
sees); when it eventually **loses**, `r` skews **positive** (static over-rates). Bottom-right
panel shows the two densities cleanly offset. This is the coherent behavior we expected, and it
was entirely hidden by the POV bug.

**The win:loss *count* gap is NOT a bug — it's the White advantage.** Decisive positions split
White-win : Black-win = **1.65 : 1** (White = 62.2% of decisive). That is genuine first-move
advantage, not a sampling artifact, because sampling is side-balanced:

- overall STM = white 50.2% / black 49.8%;
- within white-win positions: STM white 50.0% / black 50.0%;
- within black-win positions: STM white 50.7% / black 49.3%.

If a side-sampling bias were inflating White wins, the STM split *within* the win groups would be
skewed; it isn't. (62% is on the high side for self-play but plausible with adjudication + opening
randomization.)

## Consequences / recommendations

1. **The residual-by-result panel in `unc002_data_analysis.py` (panel 6) is mislabeled** — it
   plots a White-POV grouping under an "STM POV" title. Either relabel it *"White-POV"* or, better,
   switch it to `result_stm = where(is_white, result, 1 − result)`. Cosmetic only — no model
   consumes this label yet.
2. **Does this touch the model?** **No, not currently.** `train_unc_head.py` conditions on the
   embedding `x` and regresses `u = v − v*`; it does **not** use `result` as a feature or target.
   So the POV bug is confined to *this diagnostic panel* and does not corrupt any fit. **But** if
   we ever condition on or stratify by game result (e.g. result-aware margins, or a result feature
   in `x`), it **must** be converted to STM-POV first — otherwise it injects a per-ply-scrambled
   label. Flagging now so it doesn't bite later.
3. **Datagen metadata gap:** the FEN hardcodes the **fullmove counter to 1** (only the halfmove
   clock varies), so **game ply / move-number is not recoverable** from a row. unc-002.md listed
   "move number" as desired calibration metadata — it isn't actually stored. Cheap to add to the
   row schema in a future datagen pass if we want phase-by-ply studies.
4. **Extreme outlier:** `max|r| = 19,427 cp`. Spot-check whether a mate/TB `v*` sentinel is
   leaking past `kDecisiveScoreThreshold` (or a static-eval blowup). Single point — no effect on
   ν — but if it's a sentinel-filter edge case it may recur.

## Takeaway

The Student-t thesis is now quantitatively anchored (**ν ≈ 4**, kurtosis 19.4, tail-survival
tracks a t not a Gaussian/Laplace, ν-floor 2.0 non-binding). The one real defect surfaced is a
**White-POV vs STM-POV mix in the `result` label** — harmless to the current model but a landmine
for any future result-conditioned work, plus a mislabeled overview panel to fix. The win/loss
count gap is confirmed to be the legitimate White advantage, not a sampling bias.
