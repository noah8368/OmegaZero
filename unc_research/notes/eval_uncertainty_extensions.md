# Notes: Eval-Uncertainty Extensions (object O1, beyond margins)

The margin family (RFP/razoring/futility) is not the end of what the **eval-error**
distribution `p(u|x)` — object O1, the one H1–H6 already train — can drive. This note
collects the other search knobs that read the *same* head, so they cost **no new model and
no new labels**: they are the highest-leverage, lowest-risk follow-ons once O1 exists. Then
one genuinely ambitious idea (LMR-depth via information gain) that needs a depth-conditioned
O1. See [uncertainty_taxonomy.md](uncertainty_taxonomy.md) for the object map.

Convention as always (H3): `u = v − v*`, quantiles `Q_p(u|x)`, risk `C`. The recurring
figure of merit is the **predicted spread**
`σ̂(x) := Q_{1−C}(u|x) − Q_{C}(u|x)` — a per-position "how unresolved is this eval."

## 1. Aspiration windows — free, and the most obviously right

`AspirationSearch` (`engine.cc:534`) opens a fixed half-window `±aspiration_delta` around the
previous iteration's score and **doubles on every fail**, re-searching. Every fail is a
wasted search from a mis-sized window. But the window is a *prediction interval for the next
score*, which is exactly what O1 estimates:

```
current:   [ prev − δ ,  prev + δ ]            δ = aspiration_delta (constant)
O1:        [ prev − Q_{1−C/2}(u|x) ,  prev + Q_{C/2}(u|x) ]   (per-position, asymmetric)
```

Stable positions (tight `p(u|x)`) get a narrow window → fewer nodes; volatile ones get a wide
window → fewer re-searches. It **changes only the initial bounds**, so integration risk is
near-zero (H5), and it consumes the trained head verbatim. This is arguably a better *first*
integration than the margin family: no behavior is pruned away, only search bounds shift, so
a regression is bounded by re-search count, not by missed tactics.

## 2. Delta pruning in quiescence — margin family, at the leaves

Qsearch delta pruning (`engine.cc:872`, `qs_delta`) skips the whole node when
`stand_pat < alpha − qs_delta`. The per-capture form (skip a capture that can't reach α even
with its captured material) is a pure futility bet at a leaf, so it takes the O1 lower-tail
margin directly:

```
skip capture if   v + captured_value + Q_C(u|x) ≤ α
node-level:        stand_pat + Q_C(u|x) ≤ α        replaces the constant qs_delta
```

Same object, same one-sided read as futility — it just lives in `QuiescenceSearch`. Cheap win
if the O1 head is affordable at qsearch density (a real *if*: qsearch nodes dominate the tree,
so the NPS bar is highest here).

## 3. Singular extensions — the sign flip: extend when *un*resolved

`TrySingularExtension` (`engine.cc:916`) extends the TT move when a reduced search of the
alternatives fails low below `hash_entry.eval − 2·depth` — a depth-scaled **constant**
singular margin. Swap it for the O1 quantile:

```
current:   singular_beta = hash_entry.eval − 2·depth
O1:        singular_beta = hash_entry.eval − Q_{1−C}(u|x)
```

The TT move counts as "singularly better" when the alternatives fall below it by more than the
eval's *own noise floor*, not a fixed number. And this exposes the unifying statement of the
whole program:

> **Reduction and extension are one read with opposite sign.** Reduce when `σ̂(x)` is
> small (the eval is already resolved — depth buys nothing). Extend when `σ̂(x)` is large
> (the node is unresolved — depth buys information). Search *shape*, not just the prune
> decision, becomes a function of predicted resolution.

Singular extension is the "extend" pole; LMR is the "reduce" pole; O1's spread drives both.

## 4. Time management — a principled difficulty signal

OmegaZero already has dynamic, difficulty-scaled time management (the `Tm*` knobs, dynamic
soft-limit). Its "difficulty" is currently proxied from root score volatility across
iterations. O1 gives a *direct* difficulty read at the root:

```
position difficulty  ≈  σ̂(x_root)                 (wide eval band → hard → spend more)
best-move confidence  ≈  gap(top-1, top-2 move values) / (their combined spread)
```

Allocate more time when the root band is wide or the top-two gap is thin relative to spread.
This replaces a hand-crafted volatility proxy with the calibrated quantity, and reuses the
*existing* TM plumbing — a clean, low-risk consumer of the same head. (Ties into the TM-v2
work already in the tree.)

## 5. Dynamic risk `C` — the contempt/pressure lever

`C` need not be constant. It is a *risk appetite*, so it can move with game state: raise `C`
(prune/reduce harder, accept more risk) when ahead on the clock or material and wanting to
simplify; lower it in sharp or must-not-lose positions. This is a contempt-like knob with a
probabilistic meaning, and it is one scalar per heuristic — cheap to SPSA or even schedule.
Minor, but it falls out of the framing for free.

## 6. Calibration-gated fallback — the safety valve

Every O1 consumer above trusts the model. In positions far from the training manifold (weird
material, fortress, deep endgame) the model may be silently wrong. Guard it:

```
if the node looks out-of-distribution (embedding far from training support, or a running
PIT/coverage monitor has drifted)  →  fall back to the SPSA-tuned constant margin.
```

This bounds the downside of a mis-fit head to "no worse than the constant baseline" and turns
the H2 calibration diagnostics (PIT/coverage) into a *runtime* signal, not just an offline
report. Cheap insurance that makes every other extension safer to ship.

## 7. LMR-depth via information gain — the ambitious one

The only item here that needs *more* than the current O1 head: a **depth-conditioned**
`p(u | x, d)` — eval error as a function of search depth. Given it, the marginal variance
reduction from one more ply,

```
ΔI(x, d)  =  Var[u | x, d]  −  Var[u | x, d+1]
```

is the **information gained** by searching a move one ply deeper. LMR then stops being a
hand-tuned `sqrt(depth) + sqrt(movecount)` curve and becomes a *budget allocation*: reduce so
that expected information gain is equalized across moves — spend plies where they actually
resolve the eval, starve plies where they don't. This directly answers the
[pruning_integration.md](pruning_integration.md) depth open-question and would **merge with
O3** (reduced-search error is just `p(u|x,d) − p(u|x,d−R)`); see
[reduced_search_uncertainty.md](reduced_search_uncertainty.md) §"depth question." Flag as the
grand-unification path — highest payoff, highest cost, do not assume.

## Parameterization: the minimal new-param set (5)

The O1 tooling turns **one risk level `C`** + the head's `p(u|x)` into a per-position, per-tail
cp margin, so the "new param" at each site is a *scalar* (a confidence `C`, or — for the
reduction/time sites — a *slope*), never a margin. The question is how many distinct scalars
the whole rollout needs. §5 says "one scalar per heuristic"; that over-counts. Sharing by
**stakes tier** (not by site) gives the minimal defensible set:

| # | Param | Type | Sites it drives | Replaces |
|---|---|---|---|---|
| 1 | **`C_prune`** | confidence | RFP · forward futility · razoring · delta (§2) | `RfpQuantile` (exists) + `FutilityMargin` + `RazoringMargin` + `qs_delta` |
| 2 | **`C_asp`** | confidence (2-sided band, §1) | aspiration window | `aspiration_delta` |
| 3 | **`C_sing`** | confidence (§3) | singular margin | the `2·depth` constant |
| 4 | **`k_lmr`** | slope | LMR reduction modulated by `σ̂(x)` | (part of) the hand-tuned LMR curve |
| 5 | **`k_tm`** | slope | root time alloc ∝ `σ̂(x_root)` (§4) | the volatility proxy in `Tm*` |

**Why `C_prune` is one knob for four sites.** All four are near-leaf *"prune when C-confident
the corrected eval is robustly on the wrong side of a bound."* They share a **risk level**, not
a margin — RFP reads the **upper** tail (vs β), futility/razoring/delta the **lower** tail (vs
α), and the head produces the correct (asymmetric, per-position) cp margin for each. So "RFP
and futility share `C`" is right, but the justification is *equal risk tolerance*, not "their
margins are close" (the old cp-world reasoning). The thing that could later force a split is
**stakes**: RFP returns β (prunes a whole subtree) while futility skips one move — a purist
gives the higher-stakes site a more conservative `C`. **Razoring** is the likeliest first
defector (its historic margin ran ~2–4× futility's). Start shared; split only on SPRT evidence.

**Why not fewer.** Collapsing `C_prune`/`C_asp`/`C_sing` into one global `C` (→ 3 params) is
over-minimal: stakes and reversibility differ — an aspiration mis-size costs a re-search, an
RFP mis-fire misses a subtree, a singular mis-fire mis-shapes an extension — so one `C` can't
be conservative where it's irreversible *and* aggressive where it's cheap. The three-tier split
(prune / window / singular) is the parsimony-vs-expressiveness sweet spot.

**NPS corollary (see the cheapness note below).** The two int8 hidden layers dominate the head
cost and already run at every node for the corrector, so the *full* dist is nearly free once
you have them; each converted site's marginal cost is **one `QuantileCp` bisection** (~776 ns)
when it evaluates. So compute **one dist per node** and read every site's margin off it — never
re-forward per site. This makes converting many sites NPS-cheap *if* plumbed through a single
per-node dist handle. (`GetMeanCorrectionCp` vs `GetUncDistribution` differ only by the
sigma/df output rows — a few %; the real added cost is the bisection, gated per site.)
As the rollout fills in, the full dist ends up computed at every node anyway, so the
mean-only fast path is **subsumed** — the H6/H1 split resolves itself with progress. The one
place it may *survive* is **qsearch** (§2): leaf density is highest there, so a gated/mean-only
path for qsearch nodes with no active margin consumer stays the NPS-sensitive call (H5 per
§Honesty-flags).

**Optional add-ons (not part of the base 5).** Dynamic `C` (§5, a schedule on any `C_*`) and the
calibration-gated fallback (§6) add at most one scalar each and ship *alongside*, not instead of,
the above. The depth-conditioned info-gain LMR (§7) is a *different, larger* model (needs
`p(u|x,d)`); `k_lmr` here is the cheap interim that modulates the existing curve off current O1.

## Ranking for the honest roadmap

Order to actually try these, by (payoff × low-risk × reuses-existing-head):

1. **Aspiration windows** — free, near-zero integration risk, obviously right.
2. **Time management** — reuses existing TM plumbing, clear difficulty signal.
3. **Delta pruning** — margin family at leaves; gated only by qsearch NPS.
4. **Singular margin** — one-line swap; exposes the reduce/extend symmetry.
5. **Calibration-gated fallback** — ship *alongside* any of the above as insurance.
6. **Dynamic `C`** — a lever to SPSA once a consumer exists.
7. **LMR-depth info-gain** — the research stretch; needs depth-conditioned O1 + O3 merge.

## Honesty flags

- **All of §1–6 are free of new models but not free of NPS.** Each adds a head read at a new
  site; §2 (qsearch) is the riskiest because leaf density is highest. H5's kill condition
  applies per-consumer.
- **Unscheduled.** H1 (margins) is the only committed use. These are the "if H1 lands, here
  is the blast radius" list — evidence of reach, not promises.
- **Aspiration/TM regressions are bounded; margin/singular regressions are not.** Prefer to
  *demonstrate* O1's value first on the bounded-downside consumers (§1, §4-time) before the
  ones that can miss tactics.
