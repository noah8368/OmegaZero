# Notes: Move-Value Uncertainty (object O2)

Derivation of the analogues for the heuristics the margin family *cannot* cover because
they are bets about **which move is worth searching**, not about the position's eval:
**SEE pruning, LMP, LMR-amount, and move ordering**. All four are read-outs of a single
learned object — the conditional distribution of **move-value error**. See
[uncertainty_taxonomy.md](uncertainty_taxonomy.md) for where this sits (O2).

## The object

For a move `m` at position `x`, let

```
ĝ(m) = v̂ + δ(m)          cheap estimate of the value after m
                          (δ = SEE swing, or history/continuation score, or 0)
g*(m)                     true deep value after m
e(m) = ĝ(m) − g*(m)       signed move-value error
```

The object is `p(e | x, m)` — equivalently `p(g*(m) | x, m)` since `ĝ(m)` is known. It is
strictly richer than eval error `p(u|x)`: it carries a move argument. Training labels come
from self-play: for each candidate `m`, log `ĝ(m)` and the deep value `g*(m)` (a fixed-depth
OmegaZero search of the child, reusing the H4 target machinery).

## 1. SEE pruning — the clean scalar case

Current rule (`ShouldSeePrune`, `engine.h:586`): prune a capture when
`see_val < −depth · see_margin`. It is a bet that `g*(m) ≤ α` (the move won't raise alpha),
using `ĝ(m) = v̂ + see(m)`. Wrong when the move is *actually* good — a sacrifice SEE can't
see.

```
prune believes  g*(m) ≤ α
wrong when      g*(m) > α  ⟺  ĝ(m) − e(m) > α  ⟺  e(m) < ĝ(m) − α
bound           P(e(m) < ĝ(m) − α | x,m) ≤ C
lower quantile  ĝ(m) − α ≤ Q_C(e | x,m)   ⟺   ĝ(m) − Q_C(e | x,m) ≤ α
```

So **`see_margin` becomes the per-move lower-tail quantile `Q_C(e | x,m)`.** Exactly the
margin-family shape, but read off O2 instead of O1. The payoff is chess-obvious: on a
sacrifice pattern SEE badly under-predicts, the lower tail of `e` is fat, `Q_C` is large and
negative, and the move is **spared** — a *learned "don't prune the brilliancy" detector*,
where today the depth-scaled constant prunes it blindly.

## 2. LMP — from scalar margin to rank survival

LMP (`ShouldLateMovePrune`, `engine.h:571`) is the one that genuinely needs more than a
scalar: it prunes by *move count* (`num_quiet_searched > 6 + 2·depth²`), a bet that the
best remaining move is unlikely given how many we've already tried. The right object is a
distribution over the **rank of the best move** under our ordering.

Let the ordering produce moves `m_1, …, m_n` in searched order, and let `R*` be the rank of
the move that is best at full depth. LMP after `k` moves bets `R* ≤ k`. Principled rule:

```
search until  P(R* ≤ k | x) ≥ 1 − C,  then prune the tail
```

The survival function `P(R* > k | x)` **is induced by O2**: the tail contains a better move
iff some not-yet-searched move's true value beats the best searched so far,

```
P(R* > k | x) = P( max_{j>k} g*(m_j)  >  max_{i≤k} g*(m_i)  | x )
```

which is computable from the per-move laws `p(g*(m_j) | x, m_j)` (a running-max tail
probability). So LMP's move-count threshold stops being a hand-tuned polynomial in depth and
becomes a **per-position stopping rule that reads the sharpness of the ordering**: peaked
ordering (one dominant move) → stop early; flat ordering (many similar quiets) → search on.
This is the natural home for the `improving_` split, too — `improving_` is a crude proxy for
"how confident is the ordering," which O2 estimates directly.

## 3. LMR-amount — LMP made soft

LMR (`ComputeLmrReduction`, `engine.h:611`) reduces late moves instead of cutting them. It is
the **graded** version of the same bet: reduce move `m` in proportion to how unlikely it is
to matter,

```
r(m)  ∝  decreasing function of  P(g*(m) > α | x, m)  =  upper tail of e(m)
```

The current formula already gropes toward this — it nudges the reduction by the history
score (a δ proxy) and by `improving_`. O2 replaces both nudges with the actual upper-tail
probability: a move whose value-distribution has real mass above α gets a *small* reduction
(it might be the move); a move whose mass sits far below α gets a *large* one. LMP is then the
degenerate case `r = depth` (full cut) once that probability drops under `C`.

## 4. Move ordering — the same object, used for sorting not cutting

If O2 gives `p(g*(m) | x, m)` for each move, then ordering by the **mean** `E[g*(m)|x,m]` is
the Bayes-optimal expected-value order, and ordering by an **upper quantile**
`Q_{1−C}(g*(m)|x,m)` is an *optimism-under-uncertainty* order (UCB-style) that surfaces
high-variance moves — tactical shots — earlier. That is a principled unification of the
current `OrderMoves` stack (hash/SEE/killers/history/continuation) under one predictor, and
it changes the *sort key*, not the search, so integration risk is low. Whether a learned
order beats the hand-tuned one is an open SPRT question, but it is the same object for free.

## Cost reality (H5, but harsher)

O1's head rides the NNUE pass once per node. O2 needs a read **per move** at a node — dozens
of evaluations where O1 does one. This is the dominant risk for the whole object:

- Feasible only if the per-move read is *very* cheap — e.g. a tiny head on a
  cheaply-updated per-move feature (SEE, history, from/to, moving piece), **not** a full
  child-embedding forward pass.
- Or applied selectively: only at nodes where LMP/LMR/SEE-prune would fire anyway, so the
  cost is paid where it can save nodes.
- Distillation target: a monotone map from `(δ(m), a few move features)` to the needed
  quantile, so the "model" at runtime is a table/curve lookup, not a network.

## Honesty flags

- **Unscheduled and speculative.** O2 is a whole second project; nothing here is on the
  3-week plan. Recorded as derivation + object definition, ranked most-plausible first: SEE
  (clean scalar) > LMR-amount > LMP (needs the running-max machinery) > learned ordering
  (highest cost, least certain win).
- **Labels are expensive.** O1 needs one `v*` per position; O2 needs one `g*(m)` per
  *candidate move* — an order of magnitude more deep searches at datagen time.
- **The running-max survival function assumes independence** across moves' `g*` given `x`;
  they are correlated (shared position). A joint or copula treatment may be needed, or accept
  the independence approximation and let SPSA-on-`C` absorb the slack (with the usual
  calibration caveat).
