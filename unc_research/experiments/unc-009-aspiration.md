# unc-009 Milestone H — aspiration windows from the head's predicted spread

**Status:** DESIGN (draft, pre-registration) — to execute once milestone G (futility) SPRT closes.
**Site:** `Engine::AspirationSearch` (`engine.cc:554`).
**Param taxonomy:** the `C_asp` / `k_asp` row of `notes/eval_uncertainty_extensions.md` §Parameterization.
**Prereqs banked:** RFP (+11), futility (~+3). Running total toward the +100 v6 gate ≈ +14.

## Why aspiration is the right next site

- **First consumer of the head's two-sided *spread* `σ̂(x)`**, not a one-sided prune margin. Landing it
  proves the head drives *search shape*, not just prune/no-prune — the differentiated v6 story.
- **Bounded downside (the key property).** Aspiration changes only the *initial search bounds*, never which
  moves are pruned. A mis-sized window costs re-searches or a slightly-too-wide search — nodes, not
  correctness. No tactic can be missed. After the asymmetric-loss futility fight, this is a safe site to
  push on.
- **Cheap + isolable.** The window is set once per ID iteration at the *root*, whose position is fixed across
  depths — so the head forward runs **once per `GetBestMove`**, not per node. Negligible NPS.

## Current behavior

```cpp
// engine.cc:560 — symmetric constant window, doubling on every fail
int alpha = max(prev_score - aspiration_delta, kWorstEval);   // aspiration_delta = 18 cp
int beta  = min(prev_score + aspiration_delta, kBestEval);
int delta = aspiration_delta;
for (;;) {
  score = Pvs(..., alpha, beta, ...);
  if      (score <= alpha) { delta = min(delta*2, ...); alpha = score - delta; }  // fail low  -> widen down
  else if (score >= beta)  { delta = min(delta*2, ...); beta  = score + delta; }  // fail high -> widen up
  else return score;                                                              // in-window -> done
}
```

Every fail is a wasted re-search from a mis-sized window; every over-wide window is a search with weak
cutoffs. The window is a **prediction interval for the next iteration's score** — exactly what O1 estimates,
*if* we solve the scale problem below.

## The crux problem: scale mismatch (must solve, or windows blow up)

The head models `u = v − v*` — the error of the **static eval** vs a **deep** target. That spread is large
(measured earlier: `u_std ≈ 194 cp`; `Q_{0.834} − E[u]` ran ~30–370 cp). But the aspiration window predicts
the **iteration-to-iteration** score movement (depth d−1 → d), which is *much* smaller — the tuned constant is
only **18 cp**. Feeding the raw head spread into the window would make it ~10× too wide → near-full-window
searches with weak cutoffs → *slower*, the opposite of the goal.

**So the head gives the right *shape*, not the right *scale*.** The design must separate them:

```
half_width(x) = k_asp · σ̂(x)
    σ̂(x) = Q_{1−C}(u|x) − Q_C(u|x)      // head's predicted spread — the POSITION-DEPENDENT signal
    k_asp                                // global scale mapping full-error units -> iteration-delta units
```

- `σ̂(x)` carries the **relative** volatility: wide in sharp/tactical roots, narrow in quiet ones. That
  relative variation is the entire hypothesis.
- `k_asp` (SPSA-tuned, expected ≈ 0.05–0.15 given 18/194) fixes the **absolute** units, explicitly rather than
  by hiding it in an extreme quantile. `C` for `σ̂` is held fixed (e.g. 0.90) — `k_asp` absorbs scale, so `C`
  need not also be tuned. **One new param.**

## Two forms

**H-A1 (primary — symmetric):**
```
hw   = clamp(k_asp · σ̂(x), hw_min, hw_max)
alpha = max(prev − hw, kWorstEval)
beta  = min(prev + hw, kBestEval)
```
Simplest, bounded, keeps the exact doubling loop below untouched (one `delta`, seeded to `hw`).

**H-A2 (refinement — asymmetric):** split the spread into its two one-sided half-widths (same `QuantileCp`
primitive we already use), capturing the Student-t skew:
```
w_lo = clamp(k_asp · (E[u] − Q_C(u|x)),   hw_min, hw_max)     // lower half-width  >= 0
w_hi = clamp(k_asp · (Q_{1−C}(u|x) − E[u]), hw_min, hw_max)   // upper half-width  >= 0
alpha = max(prev − w_lo, ...);  beta = min(prev + w_hi, ...)
```
Needs separate `delta_lo`/`delta_hi` in the widening loop. Start with H-A1; escalate to H-A2 only if H-A1
wins and the skew looks exploitable — don't front-load the complexity.

**Widening on fail is unchanged** in spirit: on the first fail, exponential doubling (`score ± delta`) takes
over exactly as today. O1 only sizes the **initial** window — which is where most of the value is (avoid the
first re-search) and where the downside is bounded (a fail just falls back to today's doubling).

`hw_min` (a small floor, ~10 cp, so quiet roots still get a sane window and never a zero-width one) and
`hw_max` (a ceiling, so a wild head reading can't open a near-full window) bound both failure directions.

## Implementation sketch

1. **Root dist, once per search.** The root position is fixed across ID depths, so compute the head dist once
   at the top of the ID loop in `GetBestMove` (root accumulators are valid there) and cache `hw` (or
   `w_lo/w_hi`). *Not* per-`AspirationSearch`-call. The current `depth≤2 && !at_pv_node` gate never fires at
   the PV root, so this is a genuinely new, isolated read — no interaction with the RFP/futility dist path.
2. Pass the cached half-width(s) into `AspirationSearch`; seed `alpha/beta/delta` from them instead of the
   `aspiration_delta` constant.
3. New param `AspScale` (`k_asp`, `DblOpt`, divisor 1000 for a fractional scale). Keep `aspiration_delta`
   as the null/fallback and as `hw_min`-adjacent tuning if wanted. Fixed `C` as a compile constant first;
   promote to a param only if needed.

## Diagnostic BEFORE the SPRT (the fire-rate analog — deterministic, fast)

Aspiration is an **efficiency** play (same result, different node cost), so it has a clean deterministic
signal — no game noise needed to find the operating point. Instrument two counters and sweep `k_asp`:

- **aspiration fail rate** = re-searches / ID-iterations (too-tight windows → high fails).
- **nodes-to-fixed-depth** on a position suite (too-wide windows → weak cutoffs → more nodes).

The constant `aspiration_delta=18` gives a reference fail rate + node count (as `f462400`'s 9.18% did for
futility). Sweep `k_asp` to find the point that **matches-or-beats the constant's node count at a comparable
fail rate**, with `σ̂` doing the redistribution (tighter windows on quiet roots, wider on sharp). Pick that
`k_asp` as the SPRT candidate — minutes, not an SPSA run. (Lesson banked from futility: the deterministic
sweep found the optimum; SPSA only confirmed it.)

## Metrics / pass criteria (pre-registered)

- **Primary — SPRT vs the SPSA-tuned constant `aspiration_delta` null**, 10+0.1, bounds **matched to the
  expected effect** (this is small + efficiency-only → use tight bounds like `[0, 3]`, NOT `[0, 5]`; the
  futility run showed a wide bound crawls on a ~+3 effect). H1: per-position `k_asp·σ̂` beats the tuned
  constant window.
- **Secondary (deterministic, report alongside):** fail rate and nodes-to-depth vs the constant, from the
  sweep — the mechitanism behind any Elo delta.
- **NPS gate:** one extra head forward per `GetBestMove` — expected negligible; verify, don't assume.

## Interpretation / kill conditions

- **Win** → the head sizes search *bounds*, not just prunes; bank toward v6, and H-A2 (asymmetry) becomes the
  natural follow-on. Next site after: **time management** (also bounded-downside, root `σ̂` as difficulty).
- **Null (constant ties)** → position-conditional *window sizing* adds nothing over a tuned constant even
  though sharper roots are genuinely more volatile — the doubling loop already absorbs mis-sizing cheaply, so
  there's little room to win. Bank the negative, keep the constant, move on. (Bounded downside means this
  costs little to find out — the reason to do it early.)
- **Regression** → almost certainly scale (`k_asp` too high → windows too wide). The diagnostic sweep should
  prevent ever SPRT-ing a bad scale.

## Honesty flags

- **The head models the wrong variance** (full static-eval error, not iteration delta) — `σ̂` is a *proxy*
  for score volatility, rescaled by `k_asp`. The bet is that the proxy's *relative* position-to-position
  variation tracks true volatility well enough to help. Plausible (sharp positions are both harder to
  evaluate *and* more volatile across depth) but unproven — this is the scientific risk, and why the honest
  null is a tuned constant, not the old 18.
- **Effect is expected small.** Aspiration is efficiency-only; the upside is single-digit Elo at most. Its
  value in the rollout is as much *evidence of reach* (spread, not margin) as raw Elo.
