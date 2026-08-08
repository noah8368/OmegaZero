# Annotated Bibliography

References for the uncertainty-aware search work. Add a one-line "why it matters here"
to every entry — this is a working bibliography, not a citation dump.

## Normalizing flows — foundations

- **Rezende & Mohamed (2015), "Variational Inference with Normalizing Flows."** ICML.
  Introduces normalizing flows; the change-of-variables + invertible-transform framing
  we rely on. *Why:* origin of the method.

- **Papamakarios, Nalisnick, Rezende, Mohamed, Lakshminarayanan (2019/2021),
  "Normalizing Flows for Probabilistic Modeling and Inference."** JMLR survey.
  Comprehensive tour: RealNVP, Glow, MAF/IAF, NSF; conditional flows; training. *Why:*
  the map we're navigating by; primary source for architecture choice.

- **Durkan, Bekasov, Murray, Papamakarios (2019), "Neural Spline Flows."** NeurIPS.
  Monotone rational-quadratic spline couplings. *Why:* our chosen transform — expressive
  for skewed/heavy-tailed error, closed-form inverse → cheap quantiles.

- **Dinh, Sohl-Dickstein, Bengio (2017), "Density estimation using Real NVP."** ICLR.
  Affine coupling layers. *Why:* baseline flow architecture / historical context.

- **Kingma & Dhariwal (2018), "Glow."** NeurIPS. *Why:* invertible 1×1 convs; less
  relevant for 1-D but standard reference.

## Conditional density estimation & alternatives (baselines)

- **Bishop (1994), "Mixture Density Networks."** Tech report. *Why:* the MDN baseline
  (H2) — full conditional density without a flow.

- **Koenker & Bassett (1978), "Regression Quantiles."** Econometrica. Origin of
  quantile regression / the pinball (check) loss. *Why:* the QR baseline — directly
  learns the quantiles search consumes; the flow's main competition.

- **(to find)** monotone / non-crossing quantile regression networks — for a QR head
  that produces coherent, non-crossing quantiles. *Why:* avoids quantile crossing in the
  baseline; candidate for the distilled deployment head.

## Calibration & evaluation

- **Gneiting, Balabdaoui, Raftery (2007), "Probabilistic forecasts, calibration and
  sharpness."** JRSS-B. PIT + calibration/sharpness framework. *Why:* justifies PIT as
  our primary diagnostic; "calibrated *and* sharp" is exactly our objective.

- **Kuleshov, Fenner, Ermon (2018), "Accurate Uncertainties for Deep Learning Using
  Calibrated Regression."** ICML. Calibration for regression + recalibration. *Why:*
  coverage methodology and a possible post-hoc recalibration step if PIT is off.

- **(to find)** conformal prediction / conformalized quantile regression (Romano et al.
  2019). *Why:* a distribution-free way to *guarantee* coverage on the margin — could be
  a robust fallback or complement to the flow if calibration is finicky.

## Uncertainty in game-tree search (related work — for positioning the contribution)

- **(to find)** prior work on uncertainty/variance-aware alpha-beta and MCTS. Baum &
  Smith, "A Bayesian approach to game playing" style lines; B* search (Berliner) using
  optimistic/pessimistic bounds. *Why:* situate the novelty — classical search used
  interval bounds; we propose *learned, calibrated* per-position error distributions.

- **(to find)** MCTS uncertainty (e.g. uncertainty-aware PUCT / bandit variance terms).
  *Why:* nearest modern relative; contrast alpha-beta pruning-margin use vs MCTS
  exploration use.

## Correction history / eval-error correction (the mechanism we generalize)

- **(to find)** Stockfish correction-history sources / dev-forum threads — pawn,
  material, minor-piece, continuation corr-hist. *Why:* state of the art in
  *conditional-mean* eval-error correction; we position this work as its distributional
  generalization (see [correction_history.md](correction_history.md), H6). Cite the
  concrete variants OmegaZero's single pawn-hash corr-hist descends from.

## Engine / NNUE background

- **Stockfish NNUE docs + nnue-pytorch** — HalfKP/HalfKA architecture, quantization,
  training targets (score/result blend). *Why:* our embedding source; quantization
  constraints for any folded-in uncertainty head (H5).

---

*Convention:* mark unresolved lookups with `(to find)`; replace with full citation +
one-line relevance when read. Don't cite what we haven't read.
