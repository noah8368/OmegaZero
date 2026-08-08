# Notes: Correction History as Crude Conditional-Mean Error Estimation

The single most useful finding from grounding the proposal in OmegaZero's actual code:
**correction history is already a hand-rolled conditional point-estimate of evaluation
error.** The research is best framed as its distributional generalization.

## What OmegaZero does today

In `Pvs` (`src/engine.cc`):

```
raw_static_eval = board_->Evaluate();                 // ~line 631
static_eval     = GetCorrectedEval(raw_static_eval);  // ~line 632
...
// razoring (647), reverse futility (651), futility (708) all compare against static_eval
...
UpdateCorrectionHistory(raw_static_eval, best_eval, depth);  // ~line 842
```

`GetCorrectedEval` (`engine.h` ~604):

```
idx        = pawn_hash % kCorrHistSize
correction = correction_history_[player][idx]
return static_eval + correction / kCorrHistGrain      // divide is a shift (power-of-two)
```

`UpdateCorrectionHistory` nudges `correction_history_[player][idx]` toward
`(search_score − raw_static_eval)` — i.e. toward the observed error between the shallow
static eval and the deeper search result.

## Reading it as statistics

Strip the engineering and correction history is:

> a running estimate of **E[ search_score − static_eval | pawn structure ]**, keyed by a
> hash of the pawn structure, updated online during search, and folded back into the eval
> before pruning.

That is a **conditional mean of evaluation error** with:
- **conditioning variable** = pawn hash (coarse, collision-prone),
- **estimator** = online running average (fast, adaptive, no offline training),
- **output** = a scalar point estimate (mean only — no spread, no tails).

## Why this reframes the research

The uncertainty model learns `p(u | x)` where `x` is a rich NNUE embedding. Decompose it:

| Quantity | Correction history | This research |
|---|---|---|
| Conditioning | pawn hash | NNUE embedding (rich) |
| What's estimated | mean only | full conditional distribution |
| Training | online running avg | offline max-likelihood / pinball |
| Adaptivity | in-game, cheap | frozen at deploy |
| Used for | eval correction | eval correction (mean) **+** margins (quantiles) |

So the model's **mean** does correction history's job with better conditioning, and its
**quantiles** are the genuinely new capability (per-position pruning margins). This is a
*distributional generalization of an existing engine heuristic*, which is a cleaner and
more defensible contribution than "add an uncertainty module."

## Decision: replace, don't layer (2026-08-08)

We considered two integrations:
- **Additive layer** — model the error of the *corrected* eval; keep corr-hist for the
  mean, model only the residual spread. Coherent (no double-counting), low-risk, but
  keeps a worse mean estimator around and its path-dependent correction pollutes the
  label.
- **Replace** *(chosen)* — model the error of the **raw static eval**; the model's mean
  subsumes correction history (which is removed), quantiles set margins. One learned
  object, deterministic labels (raw eval is a pure fn of position), bolder claim.

Rationale for replace: if two mechanisms do the same job (the mean), don't keep the worse
one. Richer conditioning should dominate a pawn-hash scalar for the *systematic*
component of error. See [hypotheses.md](../hypotheses.md) **H6**.

## The one real risk of replacing

Correction history is **online** — it adapts within a game to position-specific eval
drift. A frozen offline model can't. The open empirical question:

> Does an NNUE-embedding-conditioned **offline** mean beat a pawn-hash-keyed **online**
> running mean?

Prior: richer features win the systematic component; online adaptivity catches
game-specific quirks the offline model misses. Net effect unknown → **measure it.**

## How to test the swap cleanly

Two *independent* experiments, never bundled:
1. **Corrector-swap (H6):** "corr-hist removed, model-mean added" as an isolated eval
   change. SPRT for Elo-neutrality-or-better *before* touching margins. If it regresses,
   fall back to a small residual online corr-hist on top of the model mean.
2. **Margins (H1):** with the mean-correction settled, add per-position quantile margins
   vs a freshly SPSA-tuned constant.

Bundling them makes a null result unattributable (bad mean? bad margins? both?).

## NPS consequence (ties to H5)

Corr-hist today is a hash lookup + shift. Replacing it with a neural mean means the
correction **must** be computed on the existing NNUE forward pass (an extra head), not via
a separate per-node MLP — otherwise NPS craters. So "replace" commits us to the
folded-in-head architecture from day one rather than as a later distillation step.

## Related-work hook for the writeup

Position against the correction-history family (pawn / material / continuation
corr-hist in modern engines) as the state of the art in *conditional-mean* eval
correction, and frame this as the first *distributional* treatment that additionally
exposes calibrated quantiles to the pruning layer. (Add citations in
[papers.md](papers.md) under a "correction history / eval error" heading.)
