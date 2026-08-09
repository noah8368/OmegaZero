# Hypotheses

Falsifiable claims driving the uncertainty-aware search work. Each has a status and
links to the experiments that test it.

**Reading order = chronology.** Sections are ordered by *when each is resolved in the
experiment sequence* — the H0 correctness gate first, the H1 payoff last — so the file
reads as the actual arc of the work (NF-001 → NF-001b → NF-002 → NF-003 → NF-004). The
chain is also a **dependency chain**: if an earlier link fails, the ones after it may be
moot. The **H-numbers are stable identifiers, not positions** — they are referenced by
number across the experiment files, notes, and log, so they stay fixed even though the
order below is not sequential.

Status legend: `open` · `supported` · `refuted` · `abandoned`

**Framing (settled 2026-08-08).** The contribution is *uncertainty-aware pruning*, not
the flow per se ("best tool wins" — the CNF is one candidate vs quantile regression /
MDN, and "flow refuted" is a fine outcome). The work is positioned as a **distributional
generalization of correction history**: OmegaZero already estimates the conditional
*mean* eval error via a pawn-hash-keyed correction (`GetCorrectedEval`); we replace it
with a learned conditional *distribution* over a rich NNUE embedding whose **mean
subsumes correction history** and whose **quantiles set pruning margins**. See
[notes/correction_history.md](notes/correction_history.md).

---

## H0 — The implementation is correct *(pre-req, not a research claim)*

**Claim.** Our conditional-density implementations (NSF flow, QR, MDN) and calibration
diagnostics (PIT, coverage, quantile-MAE) correctly recover *known* conditional
distributions `p(u|x)` — so that any later result on chess data reflects the *data*, not a
bug in the model or the metric. This is a gate, not a discovery: it must pass before any
H1–H6 result is trustworthy.

**Pass criterion (pre-registered).** On synthetic targets with closed-form CDF/quantiles:
(a) the density models recover the oracle to small **ΔNLL** (near-zero vs the generator's
own NLL); (b) **PIT** is ≈uniform and **coverage** ≈nominal at every tested quantile; (c)
tail **quantile-MAE** is small; and (d) as a *negative control*, an **unconditional**
baseline is clearly worse on all of the above — proving the diagnostics *detect* conditional
signal when it exists and *punish* its removal. Failing (d) would mean the metrics can't
tell conditioning from noise, which is as disqualifying as failing (a)–(c).

**Falsified if** a model that should recover a target cannot, or a diagnostic passes a
model that is visibly miscalibrated (or flags a correct one) — either implicates the code,
not the science, and blocks everything downstream until fixed.

**Result (2026-08-09).** **CLEARED.** flow and MDN hit ΔNLL ≈ +0.007…+0.030 nats with
near-nominal PIT/coverage and small tail qMAE; the unconditional floor blew up exactly as
required (ΔNLL up to +1.28, qMAE@.99 up to 1.6). One implementation caveat logged, not a
failure: QR's finite-difference NLL is not a proper density score (dips below oracle) — QR
is judged on PIT/coverage/quantile-MAE instead. See [NF-001](experiments/NF-001.md).

**Status:** supported (cleared) · **Experiments:** [NF-001](experiments/NF-001.md)

---

## H2 — Which conditional model is best (flow vs QR vs MDN)

**Claim.** Among a conditional Neural Spline Flow, quantile regression, and a mixture
density network, one wins on held-out NLL and calibration (PIT / coverage) for
`p(u | x)` — and we ship that one. No commitment to the flow.

**Prior.** For a *scalar* `u`, a 1-D flow reduces to learning the conditional quantile
function — close to what QR does directly, so the gap may be small. If the flow only ties
QR, the cheaper-to-deploy QR head wins on practicality (H5). This is expected and fine.

**Status:** open · **Experiments:** [NF-001](experiments/NF-001.md) (synthetic first — MDN ≥ flow on benign targets) → [NF-001b](experiments/NF-001b.md) (adversarial stress test, budget-matched, decisive)

---

## H4 — Deep-OmegaZero targets are right for the pruning goal

**Decision (settled).** `v*` = a **fixed-depth deep OmegaZero search** (with a node cap
as a safety valve). Self-consistent: pruning approximates the result of searching deeper
with OmegaZero's own eval, so error vs OmegaZero-deep is exactly what pruning risks. No
Stockfish dependency. The ground-truth-vs-external-engine calibration study is
**deferred** (would need a Stockfish target subsample).

**Status:** settled by choice · **Experiments:** NF-002

---

## H3 — Signed/directional error is the right target

**Claim.** Modeling *signed* error `v̂ − v*` (and reading one-sided quantiles) produces
better pruning than modeling `|v̂ − v*|`, because pruning is one-sided: RFP/razoring bet
the position isn't secretly *worse*; futility bets it isn't secretly *better*. **Settled
as the design choice**; the prediction to verify is that one-sided coverage conditioned
on position correlates with incorrect-cutoff rate in a way symmetric `|u|` cannot express.

**Status:** open (design fixed to signed) · **Experiments:** NF-002 schema

---

## H6 — The learned conditional mean can replace correction history *(corrector-swap)*

**Claim.** The model's conditional-*mean* error estimate (NNUE-embedding-conditioned,
offline) is at least as good an eval corrector as the current pawn-hash-keyed **online**
correction history — so correction history can be **removed** and its job absorbed by the
model, rather than layered under it.

**Design consequence.** The model targets the error of the **raw static eval**
(`Evaluate()`) — a pure, deterministic function of position (no path-dependent correction
term) — so its mean *replaces* `GetCorrectedEval`'s correction and its quantiles feed
margins. One learned object, both jobs.

**The risk being tested.** Corr-hist is *online* (adapts within a game); the model is
*frozen/offline* (richer features, no in-game adaptation). Open empirical question:
do richer features beat online adaptivity for the mean? **Must be SPRT'd on its own** —
"corr-hist removed, model-mean added" as an isolated eval change — separately from the
margin benefit (H1), or a null result is unattributable.

**Fallback if refuted.** Keep a *small residual* online corr-hist on top of the model's
stronger mean baseline (best of both, at the cost of two mechanisms).

**Status:** open · **Experiments:** (corrector-swap SPRT, post-integration)

---

## H5 — The margin is NPS-viable via a folded-in head

**Claim.** A per-node uncertainty margin (and the mean correction, per H6) can be
computed without cratering NPS by **folding the head into the existing NNUE forward
pass** — one extra output on a pass we already do. NSF's fixed-quantile inverse is
closed-form, so a distilled monotone head is cheap.

**Note.** H6 (replacing correction history) *forces* this from day one: an MLP forward
per node purely for eval correction is unaffordable (corr-hist today is a hash lookup +
shift, ~8% NPS), so the correction/quantile head must ride the NNUE pass, not sit beside
it.

**Kill condition.** If even a folded head costs more NPS than the pruning saves in nodes,
integration is a net loss regardless of calibration quality.

**Integration design:** `margin = Q_{1−C}(u|x)` reuses the existing `eval − margin ≥ β`
prune untouched — only the margin *value* changes. See
[notes/pruning_integration.md](notes/pruning_integration.md).

**Status:** open · **Experiments:** (integration spike, Week 3)

---

## H1 — Position-conditional margins beat a freshly-tuned constant *(the crux — tested last)*

**Claim.** A pruning margin conditioned on the position (via predicted evaluation-error
quantiles) yields more Elo than a single constant margin **re-tuned by SPSA under the
same conditions**.

**Why it's the crux.** OmegaZero's heuristics already scale margins by depth and
`improving_`, and (post-H6) run on mean-corrected eval — so they implicitly capture
*some* position-dependent uncertainty. The honest null is "an equally-tuned constant does
just as well." Beating the *current shipped* margins isn't enough (they could just be
under-tuned); the baseline is a **freshly SPSA-tuned constant** so a win isolates the
value of *conditioning*, not tuning effort.

**Integration.** The per-position margin *is* the model's predicted error quantile
`Q_{1−C}(u|x)`, and the SPSA-tuned constant is the **risk level `C`**; the baseline is the
*unconditional* quantile. So H1 restated: does the conditional quantile beat the
unconditional one at tuned `C`? See
[notes/pruning_integration.md](notes/pruning_integration.md) for the derivation.

**Decides:** whether this becomes a search feature or "just" a calibration study.
**Status:** open · **Experiments:** (planned, post-integration — NF-004)

---

## Stretch hypotheses H7+ *(unscheduled — reach beyond the margin family)*

Derived, not committed. These follow the *same* "estimate − conditional quantile ≥ bound"
logic as H1 but over **different uncertainty objects**; the taxonomy and per-heuristic
derivations live in [notes/uncertainty_taxonomy.md](notes/uncertainty_taxonomy.md) and its
three companions. They are recorded so the reach of the idea isn't lost, ranked by
plausibility × low-risk. **None is on the 3-week plan; H1 must land first.**

- **H7 — O1's head serves consumers beyond margins.** The *same* `p(u|x)` (object O1),
  once trained, improves aspiration-window sizing / delta pruning / singular margins / time
  management with **no new model or labels** — bounded-downside consumers (aspiration, TM)
  first. Test: SPRT each consumer swap in isolation against its tuned constant. *(open,
  most-plausible; free of new modeling cost.)*
  See [notes/eval_uncertainty_extensions.md](notes/eval_uncertainty_extensions.md).
- **H8 — Conditional ProbCut beats Gaussian ProbCut.** ProbCut (roadmap) is *defined* by a
  margin `t·σ` = the unconditional quantile; the conditional quantile `Q_{1−C}(w'|x)` should
  beat it. The cleanest, lowest-risk deployment of the whole program (margin-defined, no
  existing behavior to regress). *(open; object O3.)*
  See [notes/reduced_search_uncertainty.md](notes/reduced_search_uncertainty.md).
- **H9 — Move-value uncertainty (O2) improves SEE/LMP/LMR.** A learned per-move
  distribution `p(e|x,m)` replaces the depth-scaled SEE/LMP constants and grades LMR; its
  fat lower tail on sacrifices is a "don't prune the brilliancy" detector. A *second
  project* — new object, ~10× the datagen labels (one deep search per candidate move), and a
  per-move NPS bill. *(open; object O2, high cost.)*
  See [notes/move_uncertainty.md](notes/move_uncertainty.md).
- **H10 — Learned zugzwang detection replaces NMP's material guard.** `p(w|x,R)`'s upper
  tail auto-suppresses NMP in zugzwang, replacing the hand-coded low-material guard and
  enabling adaptive `R`. High-risk (NMP is nearly-free today; a per-node read could cost
  more than it saves). *(open; object O3.)*
- **H11 — Depth-conditioned error unifies reductions and O3 *(grand unification)*.** A
  single `p(u|x,d)` yields LMR-as-information-gain **and** reduced-search error as a
  depth difference, merging O1/O3. Highest payoff, highest cost; do not assume. *(open,
  stretch.)*
