# Notes: Reduced-Search Uncertainty (object O3)

Derivation of the analogues for heuristics whose evidence is a **shallower search**, not the
static eval: **null-move pruning** and **ProbCut** (roadmap). Their error variable is the gap
between a reduced-depth result and the deep truth — object O3 in
[uncertainty_taxonomy.md](uncertainty_taxonomy.md). The chess payoff here is specific and
sharp: the same learned distribution is a **zugzwang detector**.

## The object

For a reduced search of reduction `R` at position `x` returning `s_r`, and deep target `v*`,

```
w = s_r − v*            signed reduced-search error
```

Object: `p(w | x, R)`. It bundles two error sources that O1 doesn't have — *reduced-depth
inaccuracy* and, for NMP specifically, *the illegality of passing* (zugzwang). Both live in
the tails, which is what makes conditioning valuable.

## 1. Null-move pruning

NMP (`ShouldNullMovePrune`, `engine.h:248`) plays a null move, searches the child to reduced
depth with a null window around β, gets `s_null`, and prunes (fail-high) if `s_null ≥ β`. The
logic: if passing still leaves the opponent unable to pull us below β, a real move is at least
as good — *unless* we are in **zugzwang**, where being forced to move is bad and passing
*overstates* our value.

Cast it exactly like RFP, with `s_null` in place of `v̂` and `w` in place of `u`:

```
prune believes  v* ≥ β
observe         s_null ≥ β
wrong when      v* < β  ⟺  s_null − w < β  ⟺  w > s_null − β
bound           P(w > s_null − β | x) ≤ C
upper quantile  s_null − Q_{1−C}(w | x) ≥ β
```

So the crude `s_null ≥ β` becomes **`s_null − Q_{1−C}(w|x) ≥ β`**, `margin = Q_{1−C}(w|x)`.
And here is the chess-specific gift: **zugzwang-prone positions have a fat upper tail in
`w`** (the null overstates the truth exactly there), so `Q_{1−C}(w|x)` blows up and NMP is
**automatically suppressed** — no more hand-coded "disable NMP with only pawns / low
non-pawn material." The classic material guard becomes a *learned, continuous* zugzwang
probability read off the tail.

### 1a. Adaptive reduction `R`

`R` today is (roughly) a constant. But `p(w|x,R)` predicts the spread of the reduced search
*before* we run it, so `R` can be chosen forward-looking:

```
pick the largest R such that predicted spread of w at (x, R) stays within the cushion we
expect to the bound  →  deep where the reduced search is trustworthy, shallow (or skip)
where it isn't.
```

This replaces both the constant `R` *and* Stockfish-style verification searches with one
principled knob: verification is just "the predicted tail is too fat to trust — spend real
depth." A **calibration-gated verification** (§ eval_uncertainty_extensions "fallback")
falls out for free.

## 2. ProbCut (roadmap — the natural home of this whole program)

ProbCut isn't in the engine yet (it's on the v5 roadmap), but it is worth deriving now
because **ProbCut is literally the constant-margin baseline of this research** — the place
the idea already lives in the literature. Classic ProbCut: do a shallow search at depth
`d' < d`; if it returns `s' ≥ β + m`, assume the full-depth search would also fail high and
cut. The margin is set from a *global linear regression* of shallow-on-deep scores with
residual σ: `m = t·σ` for a fixed confidence `t`.

That global Gaussian is exactly the **unconditional** quantile. The conditional version is a
one-line swap:

```
classic:      cut if  s' − t·σ ≥ β            (σ global, Gaussian tail)
conditional:  cut if  s' − Q_{1−C}(w' | x) ≥ β   (w' = s' − v*, learned per-position)
```

with the symmetric fail-low form for the α side. **This makes ProbCut the single cleanest,
lowest-risk deployment of the entire uncertainty program**: it is *defined* by a margin, the
literature already accepts the probabilistic framing, and "conditional flow ProbCut vs
Gaussian ProbCut" is a crisp, publishable H1-shaped experiment. If O1 lands, ProbCut is the
obvious second target — and if O3's `w'` is close to O1's `u` at shallow `d'`, we may be able
to reuse the O1 head with a depth argument (see the depth open-question below).

## 3. Verification / multi-cut

Both are the same read with the sign flipped: **when the predicted reduced-search tail is
fat, don't trust one shallow result — search again.** Multi-cut ("if several moves fail high
at reduced depth, cut") is a variance-reduction trick that a calibrated `p(w|x,R)` subsumes:
the number of confirmations you demand is a function of the predicted tail, not a constant.

## The depth question, sharpened

The [pruning_integration.md](pruning_integration.md) open flag — *the label `v*` is at a
fixed depth, so the model learns error-at-that-depth, mismatched to shallow nodes* — is
**most acute for O3**, because O3's whole content is *depth mismatch* (`s_r` at `d−R` vs `v*`
deep). Two clean options:

1. Condition explicitly on `R` (or on `d` and `d'`): learn `p(w | x, R)`, a family indexed by
   reduction. More data, but it directly answers "how much does another `R` plies move this."
2. Learn error-vs-depth `p(u | x, d)` once (O1 with a depth argument) and read O3 as the
   difference of two depths. This would **merge O1 and O3** and simultaneously resolve the
   LMR-depth information-gain idea in [eval_uncertainty_extensions.md](eval_uncertainty_extensions.md).
   Ambitious; flag as the "grand unification" path, don't assume it.

## Cost reality (H5)

O3 reads are cheaper per-*node* than O2 (once per NMP/ProbCut site, not per move) but sit on
the hottest path in the tree. A read that costs more than the null/shallow search it guards
is a net loss. Feasible if it rides the NNUE pass already done at the node (O1's head with an
`R`/depth input), infeasible as a separate network.

## Honesty flags

- **NMP-with-a-margin can backfire.** NMP's value is partly that it's *cheap and
  aggressive*; adding a per-node quantile read and shrinking its reach could cost more NPS
  than the zugzwang saves. The zugzwang-detector framing is elegant but must beat the crude
  material guard *on Elo*, which is a high bar — the guard is nearly free.
- **ProbCut first, NMP later.** If we pursue O3 at all, ProbCut is the low-risk entry (no
  existing behavior to regress, margin-defined, roadmap-blessed); NMP is the high-risk one
  (fast heuristic we could easily make slower).
- **`w` is not `u`.** Tempting to reuse the O1 head for `s_null`; don't, unless the
  depth-conditioned merge (option 2 above) is actually validated. Silent reuse would model
  the wrong tail exactly where zugzwang lives.
