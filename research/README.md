# OmegaZero Research

Research track for OmegaZero. The engine's feature set is complete; this directory
holds the exploratory work that follows — currently the **uncertainty-aware search**
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
constants.

The full proposal lives in the git history / Noah's notes; the working scientific
claims are distilled into [`hypotheses.md`](hypotheses.md).

## Directory layout

```
research/
├── README.md          # this file
├── research_log.md    # chronological lab notebook — append-only, newest at top
├── hypotheses.md      # the falsifiable claims, each with a status and its evidence
├── experiments/       # one file per experiment, IDs NF-001, NF-002, ...
│   ├── NF-001.md      # synthetic validation of the flow + baselines
│   └── ...
└── notes/
    ├── normalizing_flows.md  # method notes: flows, the 1-D collapse, Zuko, quantiles
    └── papers.md             # annotated bibliography
```

## Conventions

- **Experiment IDs** are `NF-NNN`, allocated in order, never reused. Each experiment
  gets its own file from the [template](#experiment-file-template) below and is
  registered in `research_log.md` when started and when concluded.
- **Every hypothesis** in `hypotheses.md` carries a status
  (`open` / `supported` / `refuted` / `abandoned`) and links to the experiments that
  bear on it. A hypothesis is never marked resolved without a linked experiment.
- **Negative results are first-class.** "The flow did not beat quantile regression" is
  a result worth recording precisely, not a failure to hide.
- **Reproducibility.** Every experiment records the exact command, git SHA, seed, and
  data provenance needed to re-run it. Prefer committing the run script over describing
  it in prose.
- Cross-link liberally: experiments cite hypotheses, hypotheses cite experiments, notes
  cite papers.

## Experiment file template

```markdown
# NF-NNN: <short title>

- **Status:** planned | running | done | abandoned
- **Hypotheses:** H<n>, ...
- **Started / Concluded:** YYYY-MM-DD / —
- **Code / SHA:** <script path> @ <git sha>

## Question
One sentence: what does this experiment decide?

## Setup
Data, model, metrics, command to reproduce, seed.

## Results
Numbers, plots (link into figs/ or research/experiment_results/), what actually happened.

## Interpretation
What we now believe, and what it does to the linked hypotheses.

## Follow-ups
Concrete next experiments (allocate NF-IDs).
```

## Environment

Python tooling runs in the repo `.venv` (torch 2.13, numpy 2.5, matplotlib 3.11).
Additional research deps not yet installed: **scipy**, and a flow library
(**zuko** preferred — see [`notes/normalizing_flows.md`](notes/normalizing_flows.md)).
