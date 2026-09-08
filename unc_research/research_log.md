# Research Log

Chronological lab notebook. Append-only; **newest entry at top**. Keep entries short and
factual — decisions, results, dead ends, and what changed. Detailed analysis lives in
the per-experiment files under `experiments/`.

---

## 2026-09-08 — unc-008 I: SPRT set up — small QAT head (model-mean) vs main (corr-hist)

Proceeding to the H6 SPRT with the **small QAT head** (`2026-09-08_01-36-57`, v4) deployed to
`nnue/nnue_unc.bin`. TEST = this branch build (model-mean corrector, ~free NPS); BASE = `main` (2358a28,
online corr-hist). `params.json` is byte-identical on both branches, so the test is a clean
corr-hist-vs-model-mean isolation — same search params, same trunk weights (OZNN == nnue.bin), only the
corrector differs. Real clock **10+0.1** (dynamic TM active, the regime it must hold in), Threads=1/engine,
default H1 bounds (elo0=0/elo1=5, α=β=0.05). **SPSA (Phase H) and the head-size ablation (32/64/128) are
deferred to one end-of-line tuning pass**, so this is the first H6 read on untuned params. Pre-flight green:
engine plays with the v4 net (bestmove e2e4), openings.pgn present, both builds load the same trunk.

Command (run under caffeinate; SPRT auto-stops at a bound):

    caffeinate -i python3 scripts/sprt.py run --baseline-commit main \
        --tc 10+0.1 --threads 1 --concurrency 4 --max-games 30000

Watch the first game's engine-load lines: BASE (`main`) must load **NNUE** (`nnue/nnue.bin`), not fall back
to HCE — if it shows HCE, stop (LFS didn't populate the worktree net) rather than trust the result.

## 2026-09-08 — unc-008 G: output-layer per-row scale fix (OZUH v4); heads retrained w/ plots

The QAT bake-off showed the int8 corrector (P2) loss tracked **output-layer** saturation (fixed ×64 clips
the large MDN output weights, max|w|~10; 3.1% normal / 8.75% small). Fix: give the **output layer per-row
weight scales** Wo[o]=127/max|w_o| (hidden layers stay trunk-exact ×64), so the output weights quantize
without destructive clipping — same net size, same NPS. OZUH bumps to **v4** (trailing float32[4k] Wo array;
bias ×Wo[o]·127; C++ descales per row via `head_out_descale_`; v3 = uniform ×64 still readable). Python
export/read + `int8_head_params` + C++ loader/inference updated; all targets build clean.

Small head retrained QAT **with plots + --early-stop** on this fix (`2026-09-08_01-36-57`, v4, 0.000%
output clipping, Wo∈[14.2,168.3]). Parity PASS. **Result — the fix is clean but did NOT recover P2:**

| small head | P1 δ | P2 MAE-red | P3 PIT/KS | NPS |
|---|---|---|---|---|
| v3 (fixed ×64 out, 8.75% clip) | 0.81 | −6.7% | 0.511/0.028 | 0.975× |
| **v4 (per-row out, 0% clip)** | 0.81 | **−6.7%** | 0.513/0.030 | 0.966× |

**Decisive disambiguation (same 20k val sample):** small head FLOAT corrector −7.6% == INT8 corrector −7.6%
(E[u|x] differ mean 0.47 cp / p95 1.45 cp) vs the 128-wide FLOAT head's −9.6%. So **int8 is essentially
lossless for the small head; the corrector gap is pure CAPACITY (32-wide vs 128-wide), not quantization.**
This *corrects* the prior entry's read: the −6.7% was NOT output-saturation clipping — the v4 fix removes
all clipping and P2 is unchanged. The fix is still worth keeping (strictly cleaner, and it makes int8
lossless — it WILL matter for the 128-wide head, whose int8 −8.3% vs float −9.6% gap is a genuine quant
cost). The corrector/NPS tradeoff is therefore a smooth capacity curve: 32-wide −7.6% @ ~free NPS, 128-wide
−9.6%(float)/−8.3%(int8) @ −16.5% NPS; a mid width (64) is the untested sweet spot. Small head still beats
corr-hist decisively on deep-`v*` (Phase E: 106.5 vs 140.4 MAE), so it stays the SPRT candidate — the SPRT
tests whether a genuine-but-modest corrector at ~zero NPS cost gains Elo over corr-hist.

## 2026-09-08 — unc-008 G: QAT heads trained + full suite bake-off (normal vs small vs float)

Both QAT heads trained (ClippedReLU, --early-stop, trunk `nnue/nnue.bin`), fused v3, run through the full
unc-007 suite on the **C++ int8 path** (C++↔numpy int8 parity PASS, dE[u|x] ~5e-6). Reference = the original
float head.

| axis | float | normal int8 (84.8k) | small int8 (18.1k) |
|---|---|---|---|
| NPS vs 388k baseline | — | 323,878 (0.835×) | 378,425 (**0.975×**) |
| P1 Cliff's δ | 0.82 | 0.81 | 0.81 |
| P2 corrector r / MAE-red | 0.43 / −9.6% | 0.404 / −8.3% | 0.367 / −6.7% |
| P3 PIT / KS | 0.508 / 0.023 | 0.507 / 0.019 | 0.511 / 0.028 |
| P3 conditional max\|r\| | ≈0 | 0.034 | 0.046 |
| int8 output saturation | — | 3.1% | 8.75% |

**QAT worked:** calibration (P1 spread + P3 marginal/conditional) survives int8 intact for both heads — the
thing PTQ wrecked. Corrector (P2) is the cost axis: normal keeps ~86% of float's MAE benefit, small ~70%,
the loss in the sharp regime tracking output-layer saturation (fixed ×64 coarse for MDN outputs, max|w|
9–14). NPS mirrors it: small nearly free (0.975×), normal −16.5%. **Decision: small head → SPRT first**
(near-pure eval-quality test, no NPS confound, still beats corr-hist on deep-`v*`); normal is the fallback.
Likely-strictly-better follow-up: finer output-layer scale to recover the small head's P2 at the same size.
Full table + figures in [unc-007](experiments/unc-007.md#results--qat-int8-heads-through-the-suite-2026-09-08);
NPS ledger in [unc-008](experiments/unc-008.md).

## 2026-09-08 — unc-008 G: head goes QAT (like the trunk); normal vs small head bake-off

Changed tactics on H5-B. Load-time PTQ of the float head (dynamic int8) hit **317k NPS (0.82× the corr-hist
baseline, up from the 0.16× float crater)** but the int8-vs-float **mean parity was poor**: 2.6 cp mean /
8.4 cp p95 / 58 cp max over 434 sharp+quiet FENs — noise on the order of the ~9 cp corrector signal, worst
exactly where the correction is largest. So PTQ is dropped entirely (C++ load-time quant code removed) and
the head is being **retrained QAT-style, matching the trunk**: ClippedReLU `[0,1]` activations + fixed
export scales (act ×127, weights ×64 → int8, bias ×8128 → int32), integer inference with a `/64` requant —
no runtime quantization. OZUH bumps to v3 (int8 weights). The embedding cache stays valid (it's the trunk's
`[0,1]` embedding, unchanged by head QAT), so retrains are head-only/cheap.

**Two heads trained + compared on the full unc-007 suite** (pre-registered in [unc-007](experiments/unc-007.md#qat-re-validation-normal-vs-small-head-pre-registered-2026-09-08-unc-008-g)):
**normal** `512→128→128→4k` (~84.8k params) vs **small** `512→32→32→4k` (~18.1k params, ~on par with the
trunk tail's ~17.5k, mirrors its `32,32` widths). P1 (sharp/quiet spread) + P2 (mean corrector) + P3
(marginal/conditional calibration + coverage) + C++/numpy int8 parity, both heads, vs the original float
numbers. Decision = which head carries into the unc-008 SPRT: small if it holds quality (structurally
NPS-safe, per-node ≈ the trunk tail), else normal. Interim: `MeanCorrectionCp` is a float stub pending the
QAT net. Both runs: trunk `nnue/nnue.bin` (the `--trunk` default, fused in → OZNN stays byte-identical to
the deployed trunk) + `--early-stop` (patience 8, best-val checkpoint).

---

## 2026-09-07 — unc-008 opened (H6 corrector-swap + H5-B); single fused-net eval path

Pre-registered [unc-008](experiments/unc-008.md): replace the online pawn-hash correction history
with the head's conditional mean `E[u|x]` (`corrected = raw − E[u|x]`), and — in the same experiment
— make the head forward NPS-cheap (H5-B) so a naive-float crater cannot confound the SPRT vs `main`.
Enabled by unc-007 P2 (mean is a useful calibrated corrector). **Milestone commits tracked in the
unc-008 workplan table.**

Milestones so far (C++ refactor — the mean-correction/SPSA/SPRT are deferred):
- **A** (`767f953`) — unc-008 pre-registered.
- **B/C/D** (`be7098c`) — **collapsed to ONE eval code path.** The full fused net is required from here
  on: the engine defaults to & requires `nnue/nnue_unc.bin` (a missing net or a headless bare trunk is
  FATAL — no fallback). **HCE stripped entirely** (`Board::Evaluate()` NNUE-only; ~380 lines of
  handcrafted eval + `--hce`/profile removed; `ProfileForEvalMode()` always "nnue"). Corr-hist
  (`GetCorrectedEval`) is NOT HCE and stays — unc-008 Phase F replaces it. Harnesses that eval now load
  a net (datagen/bench FATAL if missing; debug/tsan load `nnue/nnue.bin`); perft is movegen-only and
  keeps the `IsLoaded` accumulator guards. `README.md` ← the research-track README. Verified: all
  targets build clean; default-loads the fused net & FATALs on a headless trunk; unc-007 parity PASS
  (head unchanged); perft startpos d5 / kiwipete d4 exact (movegen intact).

- **E** (`67bd5da`) — **three-corrector self-play diagnostic.** Added a `corrector` datagen mode (loads
  the fused net; logs `corrhist = GetCorrectedEval(v)` and `model_mean = v − E[u|x]` per sampled
  position) + `unc008_corrector_diag.py`. n=1144, depth-12 `v*`: MAE raw 115.6, **model-mean 106.5
  (−7.9%)**, corr-hist 140.4 (**+21.5% worse**); model-mean beats corr-hist head-to-head (62.8%,
  Wilcoxon p=1.4e-21). The mean is a genuine deep-truth corrector. **Caveat:** corr-hist targets
  play-depth search (here shallow st=0.1), not depth-12 `v*`, so this pre-check disadvantages it — the
  SPRT (Phase I) is the verdict. **E green-lights Phase F.** (Also fixed: datagen FATALs without a net —
  the refactor commit claimed this but missed `datagen.cc`.)

- **F** — **model-mean corrector wired live + naive-float NPS crater recorded.** `Pvs` static eval is now
  `raw − E[u|x]` via one fused forward off the shared accumulators (`dist.v_cp − round(dist.MeanCp())`;
  `v_cp == Evaluate()`, no extra trunk pass); `UpdateCorrectionHistory` dropped from the search path.
  Corr-hist tables/methods **kept dormant** (datagen diagnostic + pre-registered residual-corrhist
  fallback; delete only after the SPRT verdict). bench/debug harnesses now load the **fused** net so the
  head forward is exercised. `bench_harness 4` NPS: **388,017 → 60,510 avg (6.4×, −84%)** — the crater is
  the per-node float MLP head (`512→128→128→20`, ~84k MACs), fully attributed (kiwipete craters least:
  qsearch stand-pat stays raw). A 6.4× loss would swamp any eval gain, so **Phase G (H5-B int8 + π/μ-only)
  is mandatory before the SPRT.** Build clean (`-Werror`); ASan self-play soak on the new path clean.

Next: G (H5-B int8 mean — restore NPS to budget) → H (SPSA) → I (SPRT vs main).

---

## 2026-09-07 — unc-007: deployed head validated end-to-end; P1/P2/P3 all supported

Built the whole deployment + validation chain ([unc-007](experiments/unc-007.md)) and it landed
positive on every pre-registered axis. Details in the experiment file; the arc:

- **Fused net + C++ handle (Phase A).** New self-contained **OZNU** container = the full NNUE trunk +
  the MDN head in one `unc_research/models/nnue_unc.bin` (`oznu.py`; head trainer also emits it; OZUH
  gained a free-form `run_id`, v2). `NnueNetwork::Load` accepts it as a drop-in for `-n`, self-checks
  md5, and `EvalWithDistribution` runs the head off the **shared clamped accumulators** (H5) — no second
  feature encoding. `make unc_harness` is the only binary that calls the head (main stays head-free).
  Parity gate (`unc007_parity.py`): C++ == Python head to ~1e-6 on 35 FENs.
- **Harness + v\* (Phase B).** `unc_harness --vstar` runs datagen's exact deep target (fresh TT, depth 12,
  node cap 2M) → realized `u = v − v*`; `unc007_harness.py` writes `experiment_results/unc-007/<run>/`.
- **Powered results.** P1 via objective public suites (WAC 300 tactical vs Silent-but-Deadly 134 quiet,
  vendored under `positions/suites/`): predicted 80%-width **330 vs 105 cp, Mann-Whitney p=6e-43, Cliff's
  δ=0.82** — spread separates sharp/quiet with AUC≈0.91. P3 on val n=5000: **PIT mean 0.508, KS 0.023**,
  central coverage within ~1-2 pts of nominal — and **conditionally** calibrated too (`unc007_conditional.py`,
  n=8000: PIT independent of predicted-width/|eval|/phase, all corr≈0, coverage flat across regions →
  rules out the cancellation that marginal PIT can hide). **P2 decision** (`unc007_mean.py`, val n=20000):
  the curated null (r≈0.2) was tiny-N+OOD; as a corrector `E[u|x]` gives **r=0.43, R²=0.19, MAE −9.6% /
  RMSE −10.0%**, helping in every width regime; the binned-mean-reliability sits on the diagonal (a correct
  mean around a noisy target). **Verdict: `E[u|x]` IS a useful corrector — H6 worth pursuing** (real bar =
  beating online corr-hist, still an isolated SPRT unc-003; mean degrades far-OOD).
- **Byte-verified provenance.** `nnue/nnue.bin` == the latest NNUE run (95.5M, 2026-08-26) == the OZNN
  section of `nnue_unc.bin`; the OZUH section == the 11M head — all byte-exact (`cmp`/md5).
- **Scope decision.** OPENING-BOOK positions are **out of scope** for the whole line: the engine plays
  its book without searching, so uncertainty-aware search never runs there. Curated registry v3 drops the
  9 opening-theory positions (commented out, documented); the powered analyses are already off-book by
  construction (datagen uses random opening moves + a 10-ply skip; WAC/SBD are test positions, not book).
  Note: val FENs carry fullmove=1 (ToFen doesn't track it) and material/phase can't distinguish a book
  opening from a full-board tactical middlegame, so there is no position-intrinsic "opening" filter —
  the random-opening datagen design is what scopes it.

**Net:** the head is a strong, marginally- *and* conditionally-calibrated uncertainty (spread) predictor
whose mean is also a real corrector. Both the H1 margin (`Q_{1−C}`) and the H6 corrector-swap are now
evidence-backed. Next: engine integration — unc-004 (wire `Q_{1−C}` into one pruning margin vs a freshly-
SPSA-tuned constant) and/or the H5-B grain-quantized integer head for NPS.

---

## 2026-09-06 — unc-006 planned: deployment-aligned calibration (tail-loss + conformal), H16

The full head is well-calibrated except its top predicted-uncertainty decile (the ν≈4 tail) —
which is exactly what a margin reads. Root cause: NLL is density-weighted, so the peak dominates
the gradient and the deployed tail quantile is under-trained. Wrote up [H16](hypotheses.md#h16) +
[unc-006](experiments/unc-006.md): a cheap, head-only, no-re-datagen experiment (reuses the unc-002
head + embedding cache; splits val into a conformal-calibration fold + disjoint test fold). 2×2 over
**objective** (NLL vs NLL + tail term: CRPS / importance-weighted / aux-pinball — additive, NLL
stays the base since H2 found pure QR worst on tails) × **recalibration** (raw vs **conditional
Mondrian/CQR conformal**, per `|eval|`×phase bin, to keep the per-region guarantee not just marginal).
Metrics: one-sided tail qMAE @τ{.9,.95,.99} + per-region coverage error, guarded by bulk PIT; real
test later via H1 margins. **No NPS bill** (conformal = per-group scalar offset on the deployed grain;
tail term is train-time only) — unlike H12's trunk fine-tune. Kill: if neither arm beats
NLL+raw-quantile on tail qMAE/coverage, the headroom is the trunk ceiling (H12) or label noise
(deeper `v*`, unc-005), not the objective — which is the thing we don't yet know. It's the cheapest
of the four "improve the distribution" levers, so it goes first.

---

## 2026-09-06 — first VALID full unc-head run: well-calibrated 10.6M head (the real unc-002 read)

Full 60-epoch run on the clean data (`models/unc_head/2026-09-06_00-58-01_nnue_10634559pos`),
trunk = `nnue.bin` (md5 matches datagen) → **the first coherent 10.6M calibration read**.

- **Headline:** val NLL cond **0.8415** / uncond (floor) **1.1755** (gain +0.334); PIT KS cond
  **0.021** / uncond 0.004; coverage 52.7/82.3/**91.4**/95.8 (nominal 50/80/90/95); pinball qMAE
  cond beats floor at every τ (28.0/53.2/24.3 vs 34.3/59.7/36.2 cp). Improved over the 2-epoch
  sanity (0.888/1.177/+0.289) as expected. Slightly **conservative** (coverage a hair above
  nominal; PIT mildly ∩).
- **Extra diagnostics** (`unc_research/scripts/generate_unc_plots.py` → `figs/head_extra_diagnostics.png`):
  (1) head **learns the heteroscedastic shape** — predicted width vs |eval| and vs phase match the
  unc-002 ground-truth curves (rise, peak, decline; midgame peak). (2) **Per-region calibration is
  uniform** — central-80% coverage ~82% flat across all |v| bins (not just globally calibrated).
  (3) **Sharpness-vs-realized tracks y=x** when binned by the head's own predicted width (proper
  reliability), with only the top width-decile slightly under-covering (the ν≈4 tail is hard).
  (4) Predicted per-position width sits *below* the marginal spread within a |v| bin → the head
  conditions on **more than |v|** (sharper than a |v|-only model), a feature not a miscalibration.
  (5) **H6 preview:** conditional mean gives a modest **8.5%** median |error| reduction (68→62 cp).
- Method + pipeline fully validated end-to-end; ~7× faster than the original run. Next real steps
  are unc-003 (corrector swap SPRT) and unc-004 (margins/H1). Head export (`best.bin`) carries the
  trunk md5 + (u_mean,u_std) for deployment.

---

## 2026-09-06 — sketched unc-005: uncertainty-steered NNUE self-improvement loop (H13–H15)

Noah's idea: close a loop where the engine improves from what it learns about its own eval
uncertainty, retrains the NNUE, and iterates. Written up as [unc-005](experiments/unc-005.md)
+ hypotheses H13–H15. Framed as **iterated bootstrapping**, not RSI/FOOM — search is the only
new-information source (`v*` deeper than `v`); the uncertainty model *allocates* budget, so
each turn has a ceiling and must be **Elo-gated**. Two independent, composable mechanisms:

- **Loop A / H13 — better DATA (active learning):** acquire deep-`v*` labels on the head's
  high-uncertainty positions vs uniform sampling → more Elo per label. **Crux = H15:** must
  target **epistemic** (reducible) not **aleatoric** (the ν≈4 tactical tail) uncertainty, or
  it grabs noise and hurts; cheap epistemic estimate = small **ensemble of (tiny) heads** on
  the shared frozen trunk. Kernel test unc-005a: SPRT epistemic-K vs total-K vs random-K vs
  anti-K on Elo/label — one datagen + 4 fine-tunes + SPRT, kills the idea cheaply if random wins.
- **Loop B / H14 — better TARGETS (self-distillation):** `v̂ = v − E[u|x]` amortizes deep
  search onto cheap-`v` positions → retrain on `v̂` targets. **Non-circular only if new info
  enters each turn** (raise `v*` depth / expand the deep-searched subset). Distinct from H6
  (corrector at search time vs baked into next net's weights). Kernel test unc-005b: SPRT
  raw-`v` vs `v̂` vs gold deep-`v*` targets.

Sequencing: unc-002 head → unc-005a kernel → (if positive) unc-003 corrector → unc-005b kernel
→ only then a standing loop. Guardrails: Elo-gate every turn, per-iteration leakage discipline,
distribution-shift/forgetting monitoring, diminishing-returns tracking. Not scheduled — parked
as a first-class direction so it isn't lost; H1 (the margin crux) still comes first.

---

## 2026-09-06 — first full-size unc-head run is INVALID: training `.bin` 98% zero-filled (mtime-cache trap)

The 2026-09-05 run (`models/unc_head/2026-09-05_20-51-41_nnue_10634560pos`) reported
great-looking numbers (val NLL cond 3.02 vs uncond 7.21, gain +4.19; coverage ~nominal; PIT KS
0.05) but is **not a valid 10.6M read**. Diagnostic: `scripts/unc_head_run_diag.py` → fig
`.../figs/RUN_INVALID_zero_fill_diag.png`.

- **Root cause: `nnue/data/unc_11M/training_data.bin` is 98.36% zero-filled.** Only the first
  **174,040 records (1.64%)** are real (contiguous [0,174039], depth 12, u std 194.8 — matches
  the txt); the other **10,460,520 are all-zero** (depth 0, v=v*=u=0, empty HalfKP). The encode
  was **killed mid-run at record 174,040** before `flush()`/`truncate` (`prepare_unc_data.py`),
  so the pre-sized `mode="w+"` memmap was left full-size and zero-padded. `ensure_binary_unc`'s
  staleness check is **mtime-only** (`train_unc_head.py:341/349`) — the full-size `.bin` was newer
  than the txt, so it was accepted with no record-count/zero check. **val.bin is intact** (1.17M
  real), so val metrics ran on real data — but reflect a model trained on 174k real + 10.46M zeros.
- **Why the numbers looked good but are contaminated:** (1) standardization `u_std` computed over
  the zero-poisoned train set = **24.9cp** (true ~194) → all standardized-unit NLLs are on a wrong
  scale, not comparable to the 31k pilot's 1.023. (2) The **unconditional floor is poisoned too**
  — 98% zero targets collapse it to a near-delta at 0 → coverage **0.5% at every level**, PIT KS
  0.51 → the "+4.19 gain" is measured against a broken baseline. (3) Effective train (174k real) is
  **smaller than val (1.17M)** — absurd.
- **Silver lining / not a method failure:** trained on just 174k real, the **conditional still
  calibrated near-nominal on real val** (coverage 53.8/82.6/91.3/95.4 vs 50/80/90/95) — consistent
  with the earlier 31k pilot. The method is fine; this is pure data plumbing. Trunk md5
  **matches `nnue.bin`** (`69e1f521…`), so once the data is fixed this should be a genuine coherent
  read (still confirm the datagen labels were generated with this same net).
- **Fix:** delete `training_data.bin`, re-encode fully from txt (verify `idx == num_lines`, no
  `depth==0`), retrain. **Harden the pipeline:** encode to a temp path + atomic rename on success
  (a killed encode must never leave a "valid-looking" `.bin`), and/or have `ensure_binary_unc`
  validate record-count == txt line-count (or reject any `depth==0`) instead of trusting mtime.

- **RESOLVED 2026-09-06 (data + hardening + speedups; retrain pending Noah's go).**
  - Re-encoded `training_data.bin` cleanly: **10,634,559 records, 0 zero-filled**, u mean 13.1 /
    std 193.9 / max 19427 (matches txt). Verified.
  - **Hardened** (`prepare_unc_data.encode_uncertainty` + `train_unc_head.ensure_binary_unc`):
    encode now writes `*.bin.tmp` + **atomic `os.replace`** on success (killed encode leaves no
    final file), and `ensure_binary_unc` adds a **`bin_is_complete` guard** (last-record populated)
    so a truncated/zero-tailed `.bin` is rejected regardless of mtime.
  - **Training sped up ~7× (≈3h → ≈25min projected).** Implemented in `train_unc_head.py`:
    (A) unconditional floor now `zero_input` on a **1M subsample** — no more second ~21GB
    `zeros_like`; (B) **fp16 embeddings** + default-on `--cache auto` keyed by trunk-md5+nrows
    (11GB not 21GB); (C) chunked val + on-tensor loss accum; (D) **bs 512→8192, lr 1e-3→4e-3**
    (sqrt rule) + 1-epoch warmup; (E) **block-shuffle** contiguous batches (sequential reads;
    macOS keeps the embedding compressed → RSS ~0.6GB during training). Also **vectorized
    `embed()`** (chunked masked gather, **bit-exact** vs the per-row ref). Root of the old 3h was
    largely swap: old run held ~43GB of tensors (21GB xtr + 21GB zeros_like).
  - **2-epoch sanity on the clean data (all confirmed):** standardization correct
    (`u_std 193.9`); **floor is legit now** (val NLL 1.177, PIT KS 0.016, coverage ~nominal —
    vs the poisoned 7.21/0.51/0.5%); conditional beats it sanely — val NLL **0.888 vs 1.177**
    (+0.29), pinball sharper at every τ (28.7/54.4/25.2 vs 34.3/59.7/36.2 cp), coverage
    53.7/83.7/92.5/96.4. cond 20.4 s/epoch, floor 2.1 s/epoch. NLL still dropping fast → full
    60-epoch run will sharpen further. Trunk md5 matches `nnue.bin`, so this is a coherent read.

---

## 2026-09-05 — unc-002 tail + POV diagnostics on `unc_11M`; Student-t anchored, result-label POV bug found

Follow-up analysis on the finalized 11.8M set answering the two open questions from the
six-panel overview. New: `scripts/unc002_tail_pov_diag.py` (+ `.md`), figure
`experiment_results/unc-002/tail_pov_diag_unc11M.png`.

- **(A) Tails — Student-t thesis now quantitatively anchored.** `r = v − v*`: mean +13.1cp,
  std 194, skew +0.35, **excess kurtosis 19.4**. Method-of-moments **Student-t ν ≈ 4.3**;
  **Hill tail index α = 3.83 (right) / 4.02 (left)** — both ≈ 4. Tail-survival tracks a
  Student-t(ν≈4.3), sitting far above the Gaussian (collapses by ~400cp) and Laplace fits.
  QQ vs t(4.3) is straight through the body, bows *in* past ±3–4σ ⇒ tail **finite** ⇒ the
  planned **ν-floor 2.0 is non-binding** (headroom below observed ~4), as predicted. Tail
  mass |r|≥600 = 2.05% cross-checks the finalization note's 2.1%. Vindicates `mdn_t` over a
  Gaussian MDN. (Lone outlier max|r|=19,427cp — likely a near-sentinel v* past the filter;
  immaterial to the fit, spot-check later.)
- **(B) POV bug in `result`.** `datagen.cc` (`ResultToStr`, line ~660) stores `result`
  **White-POV**, stamped on every ply, while `v`/`v*` are **STM-POV** — so the overview's
  "residual by result (STM POV)" panel actually **mixes POVs** (every Black-to-move row
  flips). Under the buggy White-POV grouping win/loss are ~identical (mean +13/+11, skew
  +0.31/+0.24); the earlier "wins have a fat negative tail" read was an eyeball artifact.
  **Corrected STM-POV** grouping recovers the real, correctly-signed effect: STM-wins mean
  **−65** (skew −1.63), STM-loses mean **+89** (skew +1.95) — static under-rates eventual
  wins, over-rates eventual losses.
  - **It's a landmine only for FUTURE result-conditioned work** (a result feature in `x`,
    result-stratified margins) — that MUST first convert `result_stm = where(white, result,
    1−result)`. **Model impact today: none** — `train_unc_head.py` never uses `result`. The
    one live symptom, the mislabeled overview panel, is now **FIXED**: `unc002_data_analysis.py`
    converts `result`→STM-POV before panel 6, `unc_analysis_unc11M.png` regenerated (loss +89 /
    win −65, symmetric counts).
- **Win:loss count gap (1.65:1, White 62.2% of decisive) is NOT a bug** — it's the White
  first-move advantage. Sampling is side-balanced (STM 50.2/49.8 overall; 50/50 within both
  win groups), so it isn't a sampling artifact.
- **Datagen metadata gap noted:** FEN hardcodes fullmove=1 (only halfmove clock varies) ⇒
  game ply/move-number is **not recoverable** from a row; add to the schema if we want
  phase-by-ply studies.

---

## 2026-09-04 — Full 10.6M unc run landed + finalized as `unc_11M`; `NF-*`→`unc-*` rename

Pulled the coherent uncertainty run (`6325e7d`, the "50/50 STM + drop-TB-sentinel"
datagen build) from the Hetzner box and finalized it as the working research dataset.

- **Combine bug found + fixed.** A double-`mv` had nested the run inside itself
  (`unc_…/unc_…/`), so the freshly-pulled workers sat a level below where
  `combine_runs.sh` searches → it silently re-emitted a stale 866k partial pull (the
  set the committed `unc_head` had trained on). Flattened, deleted the stale
  intermediates, re-combined → **10,634,560 train + 1,178,410 val**. All 7-field,
  0 `u = v − v*` violations, STM 50.2/49.8, 0 TB/mate sentinels, v* depth 12.
- **Train/val de-leaked on the model-correct key = placement+STM** (the HalfKP
  embedding ignores move-clocks/castling/ep, so a shared placement+stm is an identical
  input `x` → true leakage even when full FENs differ). Full-FEN found only 1,305;
  placement+STM found **4,680 val rows / 3,723 positions (0.40% of val)**. Removed from
  **val** (train kept full): val → **1,173,730**; post-clean cross-split leakage = 0.
  `u` distribution unchanged and near-identical across splits (mean +13cp, |u| median
  68, p99 ~760, |u|≥600 = 2.1%) — fat pruning-relevant tail intact.
- **Disk cleanup:** kept only the 100M NNUE set + this unc set; deleted the old NNUE
  `combined/` + two stale runs + raw unc workers. **Moved + renamed** the unc set to
  **`nnue/data/unc_11M/{training,validation}_data.txt`** (was
  `nnue/data_uncertainty/combined/`; `nnue/data_uncertainty/` removed). `.bin` not
  regenerated — trainer auto-encodes on staleness.
- **Rename `NF-*` → `unc-*`** across the whole research tree (the "NF" = normalizing-flow
  label is stale since H2 refuted the flow as primary). Experiment IDs unc-001/001b/002/
  003/004, files `experiments/unc-*.md` + `experiments/unc00*_*.py`, result dirs
  `experiment_results/unc-*/`, and all in-text refs in hypotheses/README/notes/this log +
  `src/datagen.cc` comments. Import in `unc001b_stress.py` and the README `unc-NNN` ID
  convention updated too; the awk `NF==7` field guard was left untouched.
- **Path updates:** `train_unc_head.py` default `--train`/`--val` and the
  `generate_unc_plots.py` usage example now point at `nnue/data/unc_11M/`. Left the
  datagen *staging* dir (`config.json` `output`, `prepare_unc_data.py` default =
  `nnue/data_uncertainty`) alone — that's where a future raw campaign should land, not
  the finalized set.
- **Merged `main`→`research`** (CCRL-anchor Elo tooling etc.); clean, no conflicts.

## 2026-08-30 — Data-prep tooling: one `prepare_<x>_data.py` per pipeline

Consolidated the datagen→train data prep so both pipelines follow the same shape:
a single script to run before training that combines worker shards (dedup) and
encodes both splits to `.bin`.

- **New:** `scripts/prepare_nnue_data.py` (NNUE, 3-field) and
  `unc_research/scripts/prepare_unc_data.py` (uncertainty, 7-field). Each owns its encoder
  (`encode_nnue` / `encode_uncertainty` + record dtype) and runs
  `combine_runs.sh` → encode-both via a shared `combine_and_encode()` helper that
  lives in `prepare_nnue_data.py`; the unc script imports it plus `fen_to_halfkp`
  from there, so conditioning features stay byte-identical to the NNUE trunk.
- **Removed (folded in):** `preprocess_data.py`, `preprocess_uncertainty.py`,
  `prepare_uncertainty_data.sh` — the old encoder/wrapper split that made
  "prepare vs preprocess" ambiguous. Importers updated: `train_unc_head.py` and
  `plot_unc.py` now import `UNC_RECORD_DTYPE` / `encode_uncertainty` from
  `prepare_unc_data`.
- **Unchanged:** `combine_runs.sh` stays the shared, schema-aware merge step both
  prepare scripts call; trainers still auto-encode `.txt`→`.bin` on staleness, so
  the prepare step remains optional (it just pre-bakes the `.bin`).
- **Branches:** NNUE change on `main`, unc change on `research`, then merged
  `main`→`research`. Verified end to end (combine+encode round-trip, `u = v − v*`
  preserved). Earlier notes below that reference `preprocess_data.py` /
  `preprocess_uncertainty.py` now map to `prepare_nnue_data.py` /
  `prepare_unc_data.py`.

---

## 2026-08-28 — Real-data distributional read (unc run `6325e7d`) — mdn_t reaffirmed

Fresh uncertainty datagen run (`nnue/data/unc_2026-08-28_02-46-07_6325e7d`, HEAD `6325e7d`,
NNUE engine, **depth-12 `v*`**, 962,667 clean rows / 32 data + 32 val workers, 90/10 by-game
split). Checked data health and re-examined whether the H2 pick (`mdn_t`, K=5 Student-t head)
still fits the shape of real `u = v − v*`.

- **Data health — clean.** STM balance 49.8b/50.2w (the 50/50 randomized-sampling fix landed),
  90.0% train split exact, result mix 22/43/35 (loss/draw/win STM POV), depth uniformly 12,
  zero mid-file corruption. Only ~30 torn last-line records (worker shutdown-flush merges two
  rows) — fixed at the combine step by an `NF==7` guard (`combine_runs.sh`, commit `e1eaf1a`).
- **Marginal shape of `u`.** mean +12.8cp, median 5, std 194; mean|u|=120, p99=761, max 4638.
  **Excess kurtosis ≈ 10** (very heavy tails), mild positive skew (+0.23), **unimodal**,
  Laplace-like core on log-y. `|u|` carries real conditional signal: rank-corr +0.34 vs |v|,
  **−0.32 vs nodes** (few-node forcing positions are where static eval fails most — inverse
  difficulty proxy), −0.32 vs #pieces (middlegame-peaked, collapses in sparse endgames).
- **Conditional check (phase × eval-magnitude buckets, proxy for x).** All 9 buckets
  **unimodal + heavy-tailed**: bimodality coefficient 0.05–0.15 (≪ 0.555 threshold), excess
  kurtosis 3.8–17.6 *within* buckets (so the heavy tail is conditional, not a pooling artifact).
  Mean offset flips by phase (opening/quiet −9cp → endgame/decisive +27cp) — real conditional
  mean structure for the unc-003 corrector-swap to capture.
- **Verdict: `mdn_t` holds water — strengthened.** Real data is exactly the regime where the
  mixture-of-Student-t wins and the flow's only edge (multimodality) is absent: unimodal, heavy-
  tailed (kurtosis 10 → vindicates Student-t over the NaN-prone Gaussian MDN, and QR-out on
  tails), mildly skewed (mixture handles it), heteroscedastic (MDN is conditional). ν-floor 2.0
  gives infinite-variance tail headroom over the finite observed tail — not binding. Residual
  risk (conditional bimodality at full embedding resolution, invisible to these coarse proxies)
  is already hedged by the M≫K result + flow-as-backstop. **Quantiles are sampling-free**: the
  mixture CDF `Σ π_k T_cdf((y−μ_k)/σ_k; ν_k)` is closed-form-evaluable and inverted by
  deterministic 1-D bisection/Newton (already in `train_unc_head.py`); the conditional mean
  `Σ π_k μ_k` is fully closed-form (ν>1 always). This is a point *for* mdn_t vs the flow.
- **Caveat:** conditioning here is on observable proxies (phase, |v|), not the NNUE embedding
  `x` (not wired yet); full-resolution conditional shape still awaits the real-embedding re-run.
  Also this run is ~963k (pilot/mean scale), not the 2–5M the margin result (unc-004) needs.

Analysis scripts: `unc_research/scripts/unc002_data_analysis.py`,
`unc_research/scripts/unc002_cond_shape.py`; figures in `unc_research/experiment_results/unc-002/`.

---

## 2026-08-15 — unc-002 pilot data generated; fit-training harness verified end-to-end

Ran the full net-independent pilot: datagen → preprocess → frozen-trunk embed → `mdn_t` fit →
calibration read. **Everything runs.** Caveat up front: this is a **harness-verification**
pilot, not a research result — the labels are **HCE-eval** error (no `nnue.bin` at datagen
time) while the conditioning embedding is from a (weak, overfit) **NNUE** trunk, so embedding
and labels describe *different* evals. A coherent read needs NNUE-eval labels.

- **Pilot datagen** (`mode: uncertainty`, depth 8 / node_cap 500k, 1500 games, 8 workers,
  ~2.1h): **34,479 positions** (31,011 train / 3,468 val, split by game) →
  `nnue/data_uncertainty/combined/`. Clean: all 7-field, 0 `u==v−v*` violations, mean(u)=−21.8cp,
  mean|u|=124cp, **fat tail intact** (|u|≥100cp=36%, ≥300=11%, ≥600=2.6%, max 2209cp — the
  inverted filters kept the tactical tail), `v*` depth uniformly 8.
- **Trunk**: LFS-pulled `nnue/model/2026-06-07…61d0444_6.0M_pos/best.bin` (6M/3-epoch, overfit;
  the preferred 80M/1-epoch checkpoint was never saved). Embedding reconstructed **directly from
  the quantized FT block** (int16/127, clamp[0,1]) + stored HalfKP indices → 512-dim
  STM-relative accum, matching `train_nnue.py` `forward` exactly. Using the quantized weights is
  arguably *more* faithful to deployment (engine runs int16 accum) than a float `.pt` would be.
- **Fit** (`unc_research/scripts/train_unc_head.py`, `mdn_t` K=5 Student-t head):
  conditional beats the unconditional floor — **val NLL 1.023 vs 1.102** (+0.079), pinball qMAE
  lower at all quantiles (τ=.1/.5/.9). Coverage near nominal (90/95% dead on; 50% a touch under
  at 44%), PIT KS 0.044. Overfit slightly (early stop epoch 13 — 31k is small). Sanity confirmed:
  the trunk embedding carries signal about `u`, and the calibration pipeline is sound.
- **Prod gaps identified** (harness core is reusable; plumbing is not): (1) regen NNUE-coherent
  labels, (2) leakage-free positions disjoint from the net's training set, (3) the pure-Python
  per-record embed + all-in-RAM won't scale to 2–5M, (4) H5 C++ deployment inference +
  fixed-point cp-grain bake + SPSA `C` are out of scope here.

**Next — GATED ON A FULLY TRAINED NNUE.** With the pilot done, the *entire* remaining research
line now depends on the deployment-quality NNUE existing. This pilot was the **only**
net-independent piece; there is no further net-free work to do. unc-003 onward requires the real
net for two independent reasons: (1) **coherence + relevance** — labels (`v = NNUE eval`) and
the embedding must come from the *same* net, and it must be the net we'll actually ship
(uncertainty of a weak net ≠ uncertainty of the deployed net); (2) **leakage** — margin
positions must be disjoint from that net's training set, so we can't even pick positions until
the net + its corpus are fixed. So: finish NNUE training (see nnue-local-training-plan) →
`nnue/nnue.bin` → rebuild → regen datagen for NNUE-coherent labels → rerun
`train_unc_head.py` for a *real* calibration read → unc-003 corrector-swap SPRT.

---

## 2026-08-14 — H2 fully closed; unc-002 datagen built; deployment design settled

Closed H2's last asterisk and built the unc-002 label pipeline end (C++ side).

- **H2 fully closed — MDN ≥ flow, no asterisk.** Ran the **M≫K** stress run
  (`many_modes8`/`10`, modes 8/10 > MDN K=5 — the flow's last plausible win). Even
  underfitting, MDN beats the flow on tail-qMAE (Δ≈−0.02/−0.015, p=0.004) and edges it on
  CRPS (p=0.006/0.020); QR tail worst; unconditional control blows up. Verdict: KEEP SIMPLER
  MODEL. Also **fixed the Gaussian-`mdn` NaN blowup**: root cause `log(softmax)`→`log(0)`;
  grad-clipping *cannot* fix an already-NaN grad (verified byte-identical); scoped fix =
  `log_softmax` + raised σ floor (exp−6→exp−4), `mdn_t`/flow/QR byte-identical so no re-run.
  See [unc-001b](experiments/unc-001b.md).
- **unc-002 datagen — uncertainty-label mode implemented** (`src/datagen.cc`,
  config `mode: uncertainty`). Per sampled position: fixed-depth + node-capped `v*` (also
  the game move), `v = Board::Evaluate()` (raw static, STM POV), row
  `fen | v | v* | u | depth | nodes | result`; **inverted tactical filters** (keep
  in-check/tactical — the fat error tail; drop only mate band). Search is single-threaded by
  construction (bare `Engine`, not `SearchPool`) → deterministic `v*`. Smoke-tested (60 rows,
  correct). **Fixed a latent bug**: `sizeof(Engine)=566 KB` on a spawned worker's ~512 KB
  macOS stack overflowed (SIGBUS) — the existing nnue mode crashed too; Linux's 8 MB stacks
  masked it. Heap-allocated the Engine. *Open:* `preprocess_data.py` can't parse the 7-field
  row yet; corr-hist metadata deferred; warm-TT-vs-clear is an open tunable.
- **unc-002 design settled** (docs in [unc-002](experiments/unc-002.md)): fresh **~10M**
  leakage-free dataset (not the 87M HCE corpus — reuse would bias uncertainty low since those
  FENs trained the net); **frozen NNUE trunk + MDN head**; ProbCut-anchored sizing (~2–5M
  central, tail-limited by risk `C`; ProbCut fit on ~2,700 positions). **H5 deployment
  representation decided** ([hypotheses.md](hypotheses.md#h5)): head rides the shared
  incremental accumulator; distribution baked onto a **fixed-point cp grain** (C51/QR-DQN
  style) for integer inference, per-heuristic SPSA-tunable `C`.

**Next:** unc-002 training-side (parse the 7-field rows → MDN head), then unc-003 corrector-swap.

## 2026-08-09 — unc-001b Phase 1: H2 DECIDED — MDN ≥ flow, flow → backstop

Ran the adversarial, budget-matched stress test (10 seeds, `med` capacity, 4 hard
generators × 5 models). Results + table in [unc-001b](experiments/unc-001b.md).

- **No flow advantage anywhere.** On CRPS all conditional models are within ~0.5–1% with
  overlapping CIs; the flow is consistently last-or-tied-last. On tail-qMAE (what a margin
  reads) the MDN family is clearly best and the flow behind — *including* on `heavy_t` (its
  best theoretical case) and `regime_switch` (conditioner stress). Pre-registered rule fails
  on both decisive generators → **MDN primary (lean `mdn_t` — robust + best tails), flow
  demoted to backstop, QR out (worst tails).**
- **Convincing despite two caveats:** on `heavy_t` even the *mis-specified* Gaussian MDN tied
  the flow — so it's not "the exact parametric won," it's "a wrong-family parametric matched
  the flow on heavy tails." unc-001's benign read holds under stress. Clean negative result for
  the flow, exactly the pre-approved "flow refuted is fine."
- **Phase 2 (crossover) skipped** by its own rule (Phase 1 not close, no hint of a win).
- **Open TODO:** `many_modes` at `med` had M=5 = MDN K=5, so true MDN underfit (modes >
  components) was never triggered — one targeted M=8–10 run remains to fully close H2.
  Also: fix a Gaussian-MDN numerical blowup on one `regime_switch` seed.

**Next:** unc-002 (real label pipeline) builds on the **MDN** head once the NNUE embedding
exists; flow carried only as a backstop.

---

## 2026-08-09 — unc-001 full runs: H0 CLEARED, first H2 read favors MDN

Ran the full synthetic sweep (n=20k, 150 epochs, 3 seeds, 4 generators × 4 models).
Results + table in [unc-001](experiments/unc-001.md).

- **H0 gate CLEARED.** flow and MDN recover the closed-form conditionals to ΔNLL
  ≈ +0.007…+0.030 nats, PIT-KS ≈ 0.015–0.038, near-nominal coverage, small tail qMAE.
  Unconditional floor blows up as designed (ΔNLL up to +1.28, qMAE@.99 up to 1.6) — the
  diagnostics detect signal and punish its removal. Machinery is trustworthy.
- **H2 (synthetic-only) — no flow advantage; MDN quietly wins.** MDN ≥ flow on every
  generator on both ΔNLL and tail qMAE, *including* the flow-favoring cases (bimodal,
  skewnormal), and it's cheaper (closed-form CDF). Triggers the pre-registered
  "simpler-model-wins" branch; consistent with "best tool wins, flow not sacred."
- **Gotcha logged:** QR's NLL is a finite-difference artifact off its coarse quantile grid
  (dips below oracle — impossible for a real density). Judge QR on PIT-KS/coverage/qMAE
  only; there it's least-calibrated overall but best on `skewnormal` tails (its home turf).
- **Caveats:** deck mildly favors MDN (two generators *are* Gaussian mixtures); targets are
  benign. Prior, not verdict — keep the flow as a backstop for real, nastier chess error.

**Next:** designed **unc-001b** — a synthetic H2 *stress* test on adversarially hard
targets (genuinely heavy tails, sharp/asymmetric heteroscedastic multimodality) with
matched capacity/compute budgets and quantile-MAE as the primary metric, to find the
regime (if any) where the flow separates from MDN/QR before betting on real data.

---

## 2026-08-08 — Generalized the margin derivation to the rest of the tree

Pushed the [pruning_integration.md](notes/pruning_integration.md) "estimate − conditional
quantile ≥ bound" logic onto the heuristics it *doesn't* cover, and found they don't collapse
into one distribution — they split into **three uncertainty objects**:
- **O1 eval error** `u = v − v*` — what H1–H6 already train. Beyond RFP/razoring/futility it
  *also* drives aspiration windows, delta pruning, singular margins, LMR-depth, and time
  management **for free** (same head, no new labels).
- **O2 move-value error** `e(m) = ĝ(m) − g*(m)` — needs a move argument. Serves SEE pruning
  (clean scalar), LMP (via a best-move *rank survival* function induced by O2), LMR-amount
  (soft LMP), and move ordering. A whole second project (~10× labels, per-move NPS).
- **O3 reduced-search error** `w = s_r − v*` — NMP and ProbCut. Its fat upper tail *is* a
  learned zugzwang detector (auto-suppresses NMP, enables adaptive `R`); ProbCut is the
  literature's constant-margin baseline, so conditional ProbCut is the cleanest deployment.

Two framing wins: (1) **reduction and extension are one read with opposite sign** — reduce
when the eval spread is small (resolved), extend when large (unresolved); O1's spread drives
both LMR and singular extensions. (2) A depth-conditioned `p(u|x,d)` would **merge O1 and
O3** and make LMR an information-gain budget allocation — the answer to the standing
depth-mismatch flag, logged as the stretch/grand-unification path.

New notes: notes/uncertainty_taxonomy.md (umbrella + object table) and three companions
(move_uncertainty, reduced_search_uncertainty, eval_uncertainty_extensions). Registered as
stretch hypotheses **H7–H11** (unscheduled, ranked by plausibility × low-risk); scope
pointer added to pruning_integration.md. Nothing here is on the 3-week plan — H1 lands first;
this is documented *reach*, not a promise.

---

## 2026-08-08 — Pruning-integration design nailed down

Worked out the concrete bridge from `p(u|x)` to the search heuristics (Noah's framing:
one SPSA-tuned constant for "acceptable eval unreliability to prune"). Key refinement:
don't gate on unreliability alone — combine it with distance-to-bound. Deriving from
`P(false prune) = P(u > v − β | x) ≤ C` shows the rule collapses to the **existing**
prune `eval − margin ≥ β` with **`margin = Q_{1−C}(u|x)`** — the per-position error
quantile — and the single SPSA constant is the **risk level `C`** (which quantile to
read). Falls out of this:
- One quantile read does both jobs: its mean replaces corr-hist (H6), its spread is the
  margin (H1).
- H1 restated cleanly: conditional quantile vs unconditional quantile, both at tuned `C`.
- Per-heuristic, one-sided reads (RFP/razoring = upper tail, futility = lower tail) —
  the concrete payoff of signed error (H3); each can get its own `C`.
- Caveat: SPSA on `C` masks miscalibration → calibration (H2) and Elo (H1) stay separate
  axes; report both.
- Open flag: label `v*` is at a fixed depth, so the model learns error-*at-that-depth*,
  mismatched to shallow nodes — may need a depth term later.

New note: notes/pruning_integration.md (full derivation), linked from H1/H5.

---

## 2026-08-08 — Design decisions settled + correction-history reframe

Worked through the design forks from the proposal review, one at a time. Settled:

- **Flow's role:** *best tool wins.* The contribution is uncertainty-aware pruning, not
  the flow. CNF is one candidate vs QR/MDN; "flow refuted" is fine. → H1 is the headline;
  H2 demoted.
- **Truth target `v*`:** deep OmegaZero, **fixed depth** + node cap. No Stockfish (H4).
- **Error sign:** **signed** `v − v*` (H3).
- **H1 baseline:** a **freshly SPSA-tuned constant**, not the current shipped margins —
  so a win isolates *conditioning* from tuning effort.
- **Model target eval → REPLACE correction history (H6).** Grounding the proposal in the
  code revealed the key insight: `GetCorrectedEval` + `UpdateCorrectionHistory` are
  already a crude conditional-*mean* eval-error estimator (E[error | pawn hash], online).
  So the research is a **distributional generalization of correction history**. Decision:
  model the error of the **raw static eval** (deterministic, clean labels); the model's
  mean *replaces* corr-hist, its quantiles set margins. One learned object, both jobs.
  Reverses the earlier "corrected-eval additive layer" pick — replacing is cleaner and
  bolder, and Noah's "they do the same job" instinct was the better argument for it.
  - Real risk logged: corr-hist is *online*, the model is *frozen* → must SPRT the
    corrector-swap **in isolation** (unc-003) before margins (unc-004). Fallback: small
    residual online corr-hist.
  - NPS consequence: the correction/quantile head must be **folded into the NNUE forward
    pass** from day one (H5), not a later distillation.

Docs updated: hypotheses.md (H0–H6, reframed), notes/correction_history.md (new,
centerpiece), unc-002.md (schema locked). Superseded the proposal's qsearch-eval target.

**Next:** install scipy+zuko into `.venv`, scaffold `unc_research/scripts/unc001_synthetic.py`.

---

## 2026-08-08 — Research track opened

- Branched `research` off `main`. Scaffolded `unc_research/` (README, hypotheses, log,
  experiments/, notes/).
- Project: **uncertainty-aware alpha–beta search** via conditional normalizing flows.
  Proposal reviewed; distilled into five load-bearing hypotheses ([hypotheses.md](hypotheses.md))
  plus an implementation-correctness pre-req (H0).
- **Key scheduling insight.** The model conditions on the NNUE embedding, but the NNUE
  net won't exist for ~3 weeks (training data pending; SPSA on HCE params running). So
  the next 3 weeks target *net-independent* work: synthetic validation (H0/H2), the
  label+embedding pipeline shaken out on the current eval, and an integration/NPS spike.
  Real data collection waits for the net.
- **Design decisions taken up front** (to be validated, not assumed):
  - Model *signed* error, not `|error|` — pruning is one-sided (H3).
  - Build a quantile-regression baseline the flow must beat (H2); QR may also be the
    deployment path (H5).
  - Lean toward deep-OmegaZero targets over Stockfish for the pruning goal (H4).
  - The headline experiment is the constant-margin null hypothesis (H1).
- Allocated **unc-001** (synthetic validation of flow + baselines). Environment gap:
  need `scipy` and a flow lib (`zuko` preferred) in `.venv`.

**Next:** confirm zuko is the right pick, install research deps, scaffold unc-001.
