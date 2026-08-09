# Notes: A Taxonomy of Uncertainty-Aware Search

The [pruning-integration derivation](pruning_integration.md) unifies the **margin family**
(RFP / razoring / futility) as one rule — `margin = Q_{1−C}(u|x)`, the conditional error
quantile. This note is the umbrella: it asks *which other* pruning/reduction/extension
heuristics fit that pattern, and shows they do **not** all reduce to the same learned
distribution. They split into a small number of distinct **uncertainty objects**, each of
which admits the *same* "prune when the cheap estimate clears the bound by more than a
predicted quantile" derivation — but over a *different* random variable.

The point of naming the objects: the CNF/QR research (H1–H6) trains **one** of them,
`p(u | x)`. Knowing which heuristics that object can and cannot serve tells us the true
reach of the program, and what a *second* model would buy.

## The three objects

Convention throughout (from H3): signed error, "estimate − truth", truth = deep-OmegaZero
target. Each heuristic is a bet that a cheap estimate has cleared a search bound; each is
wrong when the truth sits on the other side; each false-prune probability is bounded by a
risk `C` and collapses to `estimate − Q_{1−C}(error) ≥ bound`. Only the *error variable*
changes.

| # | Object | Random variable | Conditioning | Trained by | Serves |
|---|--------|-----------------|--------------|-----------|--------|
| **O1** | **Eval error** | `u = v̂ − v*` | position `x` | H1–H6 (this program) | RFP, razoring, futility, delta pruning, aspiration windows, singular margin, LMR-depth, time mgmt |
| **O2** | **Move-value error** | `e(m) = ĝ(m) − g*(m)` | position `x`, move `m` | *new model* | SEE pruning, LMP, LMR-amount, move ordering |
| **O3** | **Reduced-search error** | `w = s_r − v*` | position `x`, reduction `R` | *new model* | NMP (+ adaptive R / zugzwang), ProbCut, multi-cut |

`v̂` = static eval; `v*` = deep target; `ĝ(m) = v̂ + δ(m)` = cheap post-move value estimate
(δ = SEE swing / history / …); `g*(m)` = deep value after `m`; `s_r` = a reduced- or
shallow-depth search return.

## Why they don't collapse into O1

- **O1 → O2** adds a move argument. `p(u|x)` says how wrong the eval of *this position* is;
  it says nothing about the *ranking* of moves out of it. SEE/LMP/LMR ask "is *this move*
  worth searching," which is `p(e | x, m)`, not `p(u | x)`. The one place they touch: with
  `p(g*(m) | x, m)` for every move you can *induce* the rank survival function LMP needs
  (see [move_uncertainty.md](move_uncertainty.md)) — so O2 is really the fundamental
  move-level object and LMP/LMR/ordering are all read-outs of it.
- **O1 → O3** replaces the static eval `v̂` with a *searched* estimate `s_r`. NMP's evidence
  is a reduced null-window result, not `Evaluate()`; its error bundles reduced-depth error
  **and** zugzwang (the pass being illegal in spirit). Different variable, different tails —
  and the tails are exactly where zugzwang lives, which is why a learned `p(w|x,R)`
  doubles as a zugzwang detector ([reduced_search_uncertainty.md](reduced_search_uncertainty.md)).

## What this buys the research narrative

1. **It bounds the headline honestly.** H1's `p(u|x)` legitimately generalizes the *margin
   family* and a surprising tail of O1 read-outs (aspiration windows, delta pruning,
   singular margins, time management) — all for free from the *same* trained head. That is
   a bigger blast radius than "three pruning constants," and worth claiming.
2. **It names the sequel cleanly.** O2 (move-value uncertainty) is a self-contained second
   project with its own headline ("learned move-value distribution replaces SEE/LMP/LMR
   constants"). O3 (reduced-search uncertainty) is a third, and its zugzwang angle is a
   crisp, chess-specific hook.
3. **It exposes one unifying idea.** Reduction and extension are the *same* read with
   opposite sign: **reduce when the eval is already resolved (spread small), extend when it
   is not (spread large).** Search *shape*, not just pruning, becomes a function of predicted
   resolution. See [eval_uncertainty_extensions.md](eval_uncertainty_extensions.md).

## The companion notes

- [move_uncertainty.md](move_uncertainty.md) — O2: SEE pruning, LMP (rank survival), LMR
  amount, move ordering.
- [reduced_search_uncertainty.md](reduced_search_uncertainty.md) — O3: NMP, adaptive `R`,
  ProbCut, verification/multi-cut.
- [eval_uncertainty_extensions.md](eval_uncertainty_extensions.md) — O1 beyond margins:
  aspiration windows, delta pruning, singular extensions, LMR-depth via information gain,
  time management, dynamic `C`, calibration-gated fallback.

## Honesty flags (apply to all three companion notes)

- These are **derivations, not results.** Only O1's margin use (H1) is scheduled. Everything
  here is "here is the analogous rule and the object it needs," ranked by plausibility, not
  a claim that it wins Elo.
- **SPSA-on-`C` masks miscalibration everywhere** (the [pruning_integration.md](pruning_integration.md)
  caveat generalizes): any of these can win on Elo with a mildly wrong model. Elo and
  calibration stay separate axes for every object.
- **Every new object is a new NPS bill** (H5). O1's head rides the NNUE pass; O2 needs a
  read *per move*, O3 a read *per NMP/ProbCut site* — much tighter budgets. Cost-viability,
  not just accuracy, gates each.
