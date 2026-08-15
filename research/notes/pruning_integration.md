# Notes: From Error Distribution to Pruning Margin

The concrete bridge from the learned conditional error distribution `p(u | x)` to the
search heuristics. This is the design NF-004 (H1) implements and NF-003 (H6) builds
toward. Relates to [hypotheses.md](../hypotheses.md) H1, H3, H5, H6 and
[correction_history.md](correction_history.md).

## The decision, stated honestly

Each margin heuristic (RFP/razoring/futility) is a bet that the eval is far enough from a
bound that searching is wasteful. The bet is wrong when a *deeper* search would have moved
the value across the bound. So the real question at a node is **not** "is the eval
unreliable?" in the abstract — it is:

> Is the eval unreliable *relative to how much cushion we have to the decision boundary?*

A pure threshold on predicted unreliability (`prune if uncertainty < C`) throws away the
cushion. Two positions with identical uncertainty are not equally safe to prune: one whose
eval barely clears β is risky; one whose eval clears β by a mile is safe even if noisy.
The uncertainty must be combined with the distance to the bound.

## Derivation: the margin *is* the predicted quantile

Convention: **signed** error `u = v − v*` (H3), where `v` is the eval used and `v*` the
deep-OmegaZero target. So `v* = v − u`.

Take reverse futility pruning (fail-high): it prunes believing `v* ≥ β`. The prune is
*wrong* exactly when `v* < β`:

```
v* < β  ⟺  v − u < β  ⟺  u > v − β
```

Bound the false-prune probability by a risk tolerance `C`:

```
P(u > v − β | x) ≤ C
```

Using the conditional quantile `Q_{1−C}(u | x)` (the value the upper tail probability C
sits above):

```
v − β ≥ Q_{1−C}(u | x)   ⟺   v − Q_{1−C}(u | x) ≥ β
```

That final form is the **existing prune** `eval − margin ≥ β`, with

```
margin(x) = Q_{1−C}(u | x)     ← per-position (1−C) error quantile
```

**So:** the per-position margin is just the model's predicted error quantile, and the
single **SPSA-tuned constant is the risk level `C`** (equivalently, *which quantile* to
read). The rule accounts for both uncertainty *and* cushion, because it compares the
quantile against `v − β`, not against a bare threshold. Integration risk stays low (H5) —
only the *value* of `margin` changes; the pruning machinery is untouched.

This is exactly "how much could more depth move this eval, at confidence 1−C?" compared
against "does that movement cross the bound?"

## Consequences

### 1. One quantile read does both jobs (H6 + H1 unified)
`Q_{1−C}(u|x)` bakes in the distribution's **mean** (which replaces correction history —
H6) and its **spread** (the margin — H1). Concretely, with `v_corrected = v − E[u|x]`:

```
v − Q_{1−C}(u|x) = v_corrected − ( Q_{1−C}(u|x) − E[u|x] )
                    └ mean-correction ┘   └── one-sided spread cushion ──┘
```

A single per-position quantile simultaneously mean-corrects the eval and sets the cushion.
The whole design collapses to one number.

### 2. H1 restated crisply
The constant-margin baseline = use the **unconditional** error quantile (same for every
position). Our version = the **conditional** quantile. So H1 is literally:

> Does the conditional quantile beat the unconditional quantile, both at the SPSA-tuned
> risk `C`?

Directly testable; a clean statement of the crux.

### 3. Per-heuristic, one-sided (why H3 matters)
Each heuristic has its own bound and direction:
- **RFP / razoring** (fail-high): upper tail of `u` — "eval secretly too high" — margin
  from `Q_{1−C}(u|x)`.
- **Futility** (fail-low): lower tail — "eval secretly too low" — margin from
  `Q_{C}(u|x)`.

Each can get its **own** SPSA-tuned `C` (they tolerate different risk). A symmetric `|u|`
quantile would mis-size the one-sided risk — this is the concrete payoff of modeling
signed error.

## Scope: this covers the margin family only

The derivation above is exact and complete for **RFP / razoring / futility** — the
heuristics that compare the static eval `v` against a bound. It does **not** subsume the
rest of OmegaZero's pruning: NMP, SEE pruning, LMP, and LMR bet on *different* random
variables (reduced-search error, move-value error), so they need *different* learned
distributions even though they share this exact "estimate − quantile ≥ bound" shape. The
full map, and the analogous derivations for each, are in
[uncertainty_taxonomy.md](uncertainty_taxonomy.md) (the umbrella) and its three companions:
[move_uncertainty.md](move_uncertainty.md) (SEE/LMP/LMR),
[reduced_search_uncertainty.md](reduced_search_uncertainty.md) (NMP/ProbCut), and
[eval_uncertainty_extensions.md](eval_uncertainty_extensions.md) (the *other* consumers of
*this* same `p(u|x)` head — aspiration windows, delta pruning, singular margins, time
management — which come for free once the margin work lands).

## Caveat: SPSA on `C` masks miscalibration
Tuning `C` for Elo will compensate for a miscalibrated model (if the "95th percentile" is
really the 90th, SPSA just shifts `C`). Good for H1 (a mildly miscalibrated model can
still win), but it means:
- A positive H1 result does **not** prove calibration, and vice-versa — **calibration
  (H2) and Elo (H1) are separate axes.** Report both.
- SPSA rescuing calibration for Elo is a feature, not a bug; the honest PIT/coverage
  numbers are still the science.

## Open questions for NF-004
- Per-heuristic `C` vs a shared `C` — start shared (fewer params), split if SPRT plateaus.
- Depth interaction: existing margins scale with depth; does `C` need to, or does the
  conditional quantile already absorb the depth-dependence? (The `v*` target depth is
  fixed at label time, so the model learns error-at-that-depth — mismatched to shallow
  nodes. May need a depth term, or per-depth models later. Flag, don't solve yet.)
- Which nodes can afford the quantile read (H5) — all pruning sites, or gate by depth?
