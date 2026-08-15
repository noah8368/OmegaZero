# Research Log

Chronological lab notebook. Append-only; **newest entry at top**. Keep entries short and
factual — decisions, results, dead ends, and what changed. Detailed analysis lives in
the per-experiment files under `experiments/`.

---

## 2026-08-15 — NF-002 pilot data generated; fit-training harness verified end-to-end

Ran the full net-independent pilot: datagen → preprocess → frozen-trunk embed → `mdn_t` fit →
calibration read. **Everything runs.** Caveat up front: this is a **harness-verification**
pilot, not a research result — the labels are **HCE-eval** error (no `nnue.bin` at datagen
time) while the conditioning embedding is from a (weak, overfit) **NNUE** trunk, so embedding
and labels describe *different* evals. A coherent read needs NNUE-eval labels.

- **Pilot datagen** (`mode: uncertainty`, depth 8 / node_cap 500k, 1500 games, 8 workers,
  ~2.1h): **34,479 positions** (31,011 train / 3,468 val, split by game) →
  `nnue/data_uncertainty/combined/`. Clean: all 7-field, 0 `u==v̂−v*` violations, mean(u)=−21.8cp,
  mean|u|=124cp, **fat tail intact** (|u|≥100cp=36%, ≥300=11%, ≥600=2.6%, max 2209cp — the
  inverted filters kept the tactical tail), `v*` depth uniformly 8.
- **Trunk**: LFS-pulled `nnue/model/2026-06-07…61d0444_6.0M_pos/best.bin` (6M/3-epoch, overfit;
  the preferred 80M/1-epoch checkpoint was never saved). Embedding reconstructed **directly from
  the quantized FT block** (int16/127, clamp[0,1]) + stored HalfKP indices → 512-dim
  STM-relative accum, matching `train_nnue.py` `forward` exactly. Using the quantized weights is
  arguably *more* faithful to deployment (engine runs int16 accum) than a float `.pt` would be.
- **Fit** (`research/experiments/nf002_fit_pilot.py`, `mdn_t` K=5 Student-t head):
  conditional beats the unconditional floor — **val NLL 1.023 vs 1.102** (+0.079), pinball qMAE
  lower at all quantiles (τ=.1/.5/.9). Coverage near nominal (90/95% dead on; 50% a touch under
  at 44%), PIT KS 0.044. Overfit slightly (early stop epoch 13 — 31k is small). Sanity confirmed:
  the trunk embedding carries signal about `u`, and the calibration pipeline is sound.
- **Prod gaps identified** (harness core is reusable; plumbing is not): (1) regen NNUE-coherent
  labels, (2) leakage-free positions disjoint from the net's training set, (3) the pure-Python
  per-record embed + all-in-RAM won't scale to 2–5M, (4) H5 C++ deployment inference +
  fixed-point cp-grain bake + SPSA `C` are out of scope here.

**Next:** NF-003 — drop the net to `nnue/nnue.bin`, rebuild, regen datagen for NNUE-coherent
labels, rerun this harness for a *real* calibration read; then the corrector-swap SPRT.

---

## 2026-08-14 — H2 fully closed; NF-002 datagen built; deployment design settled

Closed H2's last asterisk and built the NF-002 label pipeline end (C++ side).

- **H2 fully closed — MDN ≥ flow, no asterisk.** Ran the **M≫K** stress run
  (`many_modes8`/`10`, modes 8/10 > MDN K=5 — the flow's last plausible win). Even
  underfitting, MDN beats the flow on tail-qMAE (Δ≈−0.02/−0.015, p=0.004) and edges it on
  CRPS (p=0.006/0.020); QR tail worst; unconditional control blows up. Verdict: KEEP SIMPLER
  MODEL. Also **fixed the Gaussian-`mdn` NaN blowup**: root cause `log(softmax)`→`log(0)`;
  grad-clipping *cannot* fix an already-NaN grad (verified byte-identical); scoped fix =
  `log_softmax` + raised σ floor (exp−6→exp−4), `mdn_t`/flow/QR byte-identical so no re-run.
  See [NF-001b](experiments/NF-001b.md).
- **NF-002 datagen — uncertainty-label mode implemented** (`src/datagen.cc`,
  config `mode: uncertainty`). Per sampled position: fixed-depth + node-capped `v*` (also
  the game move), `v̂ = Board::Evaluate()` (raw static, STM POV), row
  `fen | v̂ | v* | u | depth | nodes | result`; **inverted tactical filters** (keep
  in-check/tactical — the fat error tail; drop only mate band). Search is single-threaded by
  construction (bare `Engine`, not `SearchPool`) → deterministic `v*`. Smoke-tested (60 rows,
  correct). **Fixed a latent bug**: `sizeof(Engine)=566 KB` on a spawned worker's ~512 KB
  macOS stack overflowed (SIGBUS) — the existing nnue mode crashed too; Linux's 8 MB stacks
  masked it. Heap-allocated the Engine. *Open:* `preprocess_data.py` can't parse the 7-field
  row yet; corr-hist metadata deferred; warm-TT-vs-clear is an open tunable.
- **NF-002 design settled** (docs in [NF-002](experiments/NF-002.md)): fresh **~10M**
  leakage-free dataset (not the 87M HCE corpus — reuse would bias uncertainty low since those
  FENs trained the net); **frozen NNUE trunk + MDN head**; ProbCut-anchored sizing (~2–5M
  central, tail-limited by risk `C`; ProbCut fit on ~2,700 positions). **H5 deployment
  representation decided** ([hypotheses.md](hypotheses.md#h5)): head rides the shared
  incremental accumulator; distribution baked onto a **fixed-point cp grain** (C51/QR-DQN
  style) for integer inference, per-heuristic SPSA-tunable `C`.

**Next:** NF-002 training-side (parse the 7-field rows → MDN head), then NF-003 corrector-swap.

## 2026-08-09 — NF-001b Phase 1: H2 DECIDED — MDN ≥ flow, flow → backstop

Ran the adversarial, budget-matched stress test (10 seeds, `med` capacity, 4 hard
generators × 5 models). Results + table in [NF-001b](experiments/NF-001b.md).

- **No flow advantage anywhere.** On CRPS all conditional models are within ~0.5–1% with
  overlapping CIs; the flow is consistently last-or-tied-last. On tail-qMAE (what a margin
  reads) the MDN family is clearly best and the flow behind — *including* on `heavy_t` (its
  best theoretical case) and `regime_switch` (conditioner stress). Pre-registered rule fails
  on both decisive generators → **MDN primary (lean `mdn_t` — robust + best tails), flow
  demoted to backstop, QR out (worst tails).**
- **Convincing despite two caveats:** on `heavy_t` even the *mis-specified* Gaussian MDN tied
  the flow — so it's not "the exact parametric won," it's "a wrong-family parametric matched
  the flow on heavy tails." NF-001's benign read holds under stress. Clean negative result for
  the flow, exactly the pre-approved "flow refuted is fine."
- **Phase 2 (crossover) skipped** by its own rule (Phase 1 not close, no hint of a win).
- **Open TODO:** `many_modes` at `med` had M=5 = MDN K=5, so true MDN underfit (modes >
  components) was never triggered — one targeted M=8–10 run remains to fully close H2.
  Also: fix a Gaussian-MDN numerical blowup on one `regime_switch` seed.

**Next:** NF-002 (real label pipeline) builds on the **MDN** head once the NNUE embedding
exists; flow carried only as a backstop.

---

## 2026-08-09 — NF-001 full runs: H0 CLEARED, first H2 read favors MDN

Ran the full synthetic sweep (n=20k, 150 epochs, 3 seeds, 4 generators × 4 models).
Results + table in [NF-001](experiments/NF-001.md).

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

**Next:** designed **NF-001b** — a synthetic H2 *stress* test on adversarially hard
targets (genuinely heavy tails, sharp/asymmetric heteroscedastic multimodality) with
matched capacity/compute budgets and quantile-MAE as the primary metric, to find the
regime (if any) where the flow separates from MDN/QR before betting on real data.

---

## 2026-08-08 — Generalized the margin derivation to the rest of the tree

Pushed the [pruning_integration.md](notes/pruning_integration.md) "estimate − conditional
quantile ≥ bound" logic onto the heuristics it *doesn't* cover, and found they don't collapse
into one distribution — they split into **three uncertainty objects**:
- **O1 eval error** `u = v̂ − v*` — what H1–H6 already train. Beyond RFP/razoring/futility it
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
`P(false prune) = P(u > v̂ − β | x) ≤ C` shows the rule collapses to the **existing**
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
- **Error sign:** **signed** `v̂ − v*` (H3).
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
    corrector-swap **in isolation** (NF-003) before margins (NF-004). Fallback: small
    residual online corr-hist.
  - NPS consequence: the correction/quantile head must be **folded into the NNUE forward
    pass** from day one (H5), not a later distillation.

Docs updated: hypotheses.md (H0–H6, reframed), notes/correction_history.md (new,
centerpiece), NF-002.md (schema locked). Superseded the proposal's qsearch-eval target.

**Next:** install scipy+zuko into `.venv`, scaffold `research/experiments/nf001_synthetic.py`.

---

## 2026-08-08 — Research track opened

- Branched `research` off `main`. Scaffolded `research/` (README, hypotheses, log,
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
- Allocated **NF-001** (synthetic validation of flow + baselines). Environment gap:
  need `scipy` and a flow lib (`zuko` preferred) in `.venv`.

**Next:** confirm zuko is the right pick, install research deps, scaffold NF-001.
