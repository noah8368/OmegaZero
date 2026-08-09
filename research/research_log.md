# Research Log

Chronological lab notebook. Append-only; **newest entry at top**. Keep entries short and
factual — decisions, results, dead ends, and what changed. Detailed analysis lives in
the per-experiment files under `experiments/`.

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
