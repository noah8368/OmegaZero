# OmegaZero Research

OmegaZero is a UCI chess engine whose feature set is complete; the project's active work is
now the research track in [`unc_research/`](unc_research) — the **uncertainty-aware search**
line: learning a conditional distribution over evaluation error and feeding calibrated
quantiles into the pruning/reduction heuristics.

This is deliberately separate from the engine's normal development flow. Work here is
allowed to be speculative, half-finished, and negative-result-heavy. The point is to
keep a rigorous, honest paper trail so that (a) nothing gets lost across context
windows and long gaps, and (b) the eventual writeup has real evidence behind it.

## Current project

**Learning Evaluation Uncertainty with Conditional Normalizing Flows for
Uncertainty-Aware Alpha–Beta Search.**

Given a learned representation of a position (NNUE embedding), predict the *conditional
distribution* of evaluation error `p(u | x)` rather than a point estimate. Read
calibrated quantiles (e.g. the 95th/99th percentile of error) off that distribution and
use them to set pruning margins per-position instead of using fixed, globally-tuned
constants — and use the distribution's *mean* as a learned eval corrector (a distributional
generalization of correction history).

The working scientific claims are distilled into
[`unc_research/hypotheses.md`](unc_research/hypotheses.md); the chronological lab notebook is
[`unc_research/research_log.md`](unc_research/research_log.md).

## Engine

The engine is a HalfKP-NNUE, alpha–beta searcher (CCRL-scale ≈ 2348 Elo). As of unc-008 it
runs a **single eval code path**: the fused net (trunk + uncertainty head, `nnue/nnue_unc.bin`)
is required; the handcrafted-eval fallback has been removed.

```
make                                  # build build/OmegaZero (requires nnue/nnue_unc.bin)
build/OmegaZero --uci                 # UCI mode
build/OmegaZero -p w --st 5           # play as White, 5s/move
build/unc_harness < fens.txt          # per-position eval + p(u|x) (research)
```

## Directory layout

```
unc_research/
├── README.md          # research-track intro (mirrors this file)
├── research_log.md    # chronological lab notebook — append-only, newest at top
├── hypotheses.md      # the falsifiable claims, each with a status and its evidence
├── experiments/       # one file per experiment, IDs unc-001, unc-002, ...
├── models/            # trained unc heads + the fused nnue_unc.bin
├── positions/         # curated + public (WAC / Silent-but-Deadly) position suites
├── scripts/           # oznu.py (fused-net format), harness drivers, analysis
└── notes/             # method notes + annotated bibliography
```

## Conventions

- **Experiment IDs** are `unc-NNN`, allocated in order, never reused. Each experiment gets its
  own file and is registered in `research_log.md` when started and when concluded.
- **Every hypothesis** carries a status (`open` / `supported` / `refuted` / `abandoned`) and
  links to the experiments that bear on it. A hypothesis is never marked resolved without a
  linked experiment.
- **Negative results are first-class.** "The flow did not beat quantile regression" is a result
  worth recording precisely, not a failure to hide.
- **Reproducibility.** Every experiment records the exact command, git SHA, seed, and data
  provenance needed to re-run it. Prefer committing the run script over describing it in prose.

## Environment

Python tooling runs in the repo `.venv` (torch, numpy, matplotlib, scipy). The C++ engine and
harnesses build with `make` (Google style, `-Wall -Werror -Wextra -Wshadow`).
