# Notes: Normalizing Flows for Conditional Error Density

Working method notes for modeling `p(u | x)` — the distribution of evaluation error `u`
conditioned on a position representation `x`.

## What a normalizing flow is

A flow transforms a simple base density (standard Gaussian `z`) into a target density
via an invertible, differentiable map `x = f(z)`. Density comes from change-of-variables:

```
p(x) = p(z) · |det ∂f⁻¹/∂x|
```

Because `f` is invertible with a tractable Jacobian, you can train by **exact maximum
likelihood** — no ELBO, no sampling estimator. That exactness is the main reason to
prefer flows over VAEs/GANs for density estimation.

**Conditional** flows make the transform depend on `x`: `u = f(z; x)`. The conditioning
info is fed into each coupling/transform layer, so different positions induce different
error distributions while sharing one Gaussian latent.

## The 1-D collapse (important for us)

Our target `u` is **one-dimensional**. In 1-D a normalizing flow is *just a learned
conditional monotonic warp of a Gaussian*:

```
z = T(u; x),   z ~ N(0,1),   T monotone in u
```

Consequences:
- The Jacobian is a scalar derivative `|T'(u; x)|` — trivial.
- The conditional CDF is `F(u | x) = Φ(T(u; x))`.
- The **q-quantile is closed-form**: `u_q = T⁻¹(Φ⁻¹(q); x)`. For a Neural Spline Flow
  the inverse `T⁻¹` is analytic, so a fixed quantile (say q=0.95) costs one conditioner
  forward pass + one spline inverse. `Φ⁻¹(q)` is a compile-time constant.

The flip side: because a 1-D CNF *is* essentially "learn the conditional quantile
function," it sits very close to **quantile regression**, which learns quantiles
directly via the pinball loss. This is exactly why NF-001 pits them against each other
(H2) — the flow has to justify its extra machinery on calibration, and if it only ties
QR, QR likely wins on deployment simplicity (H5).

## Architecture pick: Neural Spline Flows (NSF)

From Papamakarios et al. (2019) survey and Durkan et al. (2019):
- **RealNVP / Glow** — affine coupling; limited expressiveness per layer.
- **MAF / IAF** — autoregressive; for 1-D these degenerate (no ordering to exploit).
- **Neural Spline Flows** — monotone rational-quadratic splines. Best fit here:
  represent skewed, heavy-tailed distributions (eval error has a fat right tail) with
  few parameters, and the spline inverse is closed-form → cheap quantiles.

For a scalar target conditioned on a vector `x`, the natural construction is a single
**conditional monotone rational-quadratic spline** whose knot parameters are produced by
a small MLP conditioner `x → (widths, heights, derivatives)`. This is effectively a
conditional NSF with one transform, and is also a clean drop-in for the QR baseline
(same conditioner, different loss/head).

## Library: Zuko (preferred)

- `zuko` — modern, thin PyTorch flow lib; `zuko.flows.NSF(features=1, context=dim_x)`
  gives a conditional neural spline flow out of the box; `flow(x).log_prob(u)` for
  training, `flow(x).sample()` / inverse for quantiles. Clean conditional API, actively
  maintained, minimal boilerplate. **Not yet installed** in `.venv`.
- `nflows` — older, more code to wire conditioning; heavier. Fallback only.
- Roll-our-own 1-D conditional spline — genuinely viable given the 1-D collapse, and
  removes a dependency, but defer unless zuko fights us.

**Decision (pending NF-001 smoke test):** start with zuko for velocity; keep the
hand-rolled spline as a fallback and as the eventual thing we distill into for the
engine.

## Baselines the flow must beat (NF-001, H2)

1. **Quantile regression** — MLP `x → {q-quantiles}` trained with pinball loss.
   Monotone-in-q via sorting or a monotone parameterization. Directly optimizes the
   quantity search consumes; trivial single-pass inference. Strongest baseline.
2. **Mixture density network** — MLP `x → {π_k, μ_k, σ_k}` of a Gaussian (or
   log-normal) mixture. Gives a full density like the flow; quantiles via numeric
   inversion of the mixture CDF.
3. (sanity) **Homoscedastic / unconditional** — a single fitted distribution ignoring
   `x`. If the conditional models don't beat *this*, there's no position signal at all.

## Calibration diagnostics

- **PIT** (Probability Integral Transform): `r_i = F(u_i | x_i)`. If the model is
  calibrated, `{r_i}` is Uniform(0,1). Histogram shape diagnoses failure mode:
  - flat → calibrated
  - mass near 0 and 1 → distributions too narrow (overconfident)
  - mass near 0.5 → too wide (underconfident)
  - one-sided mass → systematic bias (relevant to signed-error H3)
- **Empirical coverage**: fraction of `u_i` below predicted q-quantile should ≈ q.
  Evaluate globally *and* stratified (game phase, eval range, tactical vs quiet) — the
  whole thesis is that uncertainty varies by position, so per-stratum coverage is the
  real test.

## Inference-cost sketch (H5)

Per node, for a fixed quantile q:
- conditioner MLP forward on `x` (the NNUE embedding we already computed) → spline params
- spline inverse at the constant `Φ⁻¹(q)` → the margin

The conditioner is the cost. Mitigations: (a) fold it into the NNUE forward pass as an
extra head so it rides along with an eval we already do; (b) distill the flow's q=0.95
margin into a single cheap monotone head (QR-style) for the shipped engine, keeping the
flow offline for the science. Measure NPS before believing any of this.

## Open method questions

- One conditional spline vs a small stack — does depth help for a 1-D target, or just
  overfit? (NF-001)
- Best conditioner input: raw NNUE accumulator vs final hidden layer vs a dedicated
  embedding. (needs the trained net; deferred)
- Log-space modeling if we ever revert to `|u|` (nonnegative, heavy tail) vs signed
  error in linear space. Leaning signed/linear per H3.
