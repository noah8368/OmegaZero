#!/usr/bin/env python3
"""unc-001b: H2 stress test — does the flow separate from MDN/QR on *hard* targets?

unc-001 found MDN >= flow, but on benign targets (two were literally Gaussian mixtures),
with unmatched budgets and 3 seeds. This harness fixes all three so the H2 decision is
fair and decisive:

  1. Adversarial generators carrying the pathologies we expect in eval error:
       heavy_t        genuinely heavy (polynomial) tails       -> the clean MDN-killer
       many_modes     more modes than the MDN has components   -> MDN underfit test
       regime_switch  discontinuous p(u|x)                     -> stresses the conditioner
       hetero_skew    sign-flipping skew (both tails)          -> H3 asymmetry test
       bounded_edge   moving hard support edge (mate analogue) -> optional
  2. A fair fight: shared trunk width, a capacity preset (or sweep), an MDN K sweep, a
     Student-t-component MDN variant, and per-model parameter-count / latency reporting.
  3. Deployment-relevant, model-agnostic metrics: CRPS (proper, works for QR without the
     finite-difference NLL hack), both-tail pinball + quantile-MAE, PIT-KS, and coverage
     *stratified by x* — with multi-seed mean +/- 95% CI and a paired flow-vs-best test.

Phase 1 decides yes/no on flow separation (with CIs and a pre-registered verdict).
Phase 2 maps the crossover: how heavy a tail / how many modes before the flow overtakes
MDN, so unc-002 can check whether real chess error lands in the flow-winning regime.

See research/experiments/unc-001b.md for the design and research/hypotheses.md (H2/H3/H5).

Usage:
    # Phase 1 (fair comparison), full rigor:
    python research/experiments/unc001b_stress.py --phase 1 --seeds 10 --capacity med \
        --outdir research/experiment_results/unc-001b
    # Capacity frontier instead of a single preset:
    python research/experiments/unc001b_stress.py --phase 1 --capacity sweep
    # Phase 2 (crossover sweep) — only if Phase 1 is close:
    python research/experiments/unc001b_stress.py --phase 2 --seeds 5
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import torch
import torch.nn as nn
from scipy import stats

import zuko

# Reuse the shared plumbing from unc-001 so the two harnesses can't drift apart.
from unc001_synthetic import (
    CTX_DIM,
    DEVICE,
    Generator,
    Model,
    UnconditionalModel,
    _sigmoid,
    _to_t,
    _train_loop,
    sample_context,
)

# Deployment-relevant quantiles: both tails (H3 — pruning is one-sided per heuristic).
DEPLOY_Q = (0.01, 0.05, 0.10, 0.90, 0.95, 0.99)
# Levels at which we report coverage / quantile-MAE (adds the median as a sanity anchor).
EVAL_LEVELS = (0.01, 0.05, 0.10, 0.50, 0.90, 0.95, 0.99)
# The extreme both-tail levels that define the scalar "tail-qMAE" decision metric.
TAIL_LEVELS = (0.01, 0.05, 0.95, 0.99)
CRPS_SAMPLES = 128  # stratified inverse-CDF samples per point for the CRPS estimate
N_STRATA = 4  # x[:,0] quartile bins for stratified coverage


# ---------------------------------------------------------------------------
# Adversarial generators. Each keeps a closed-form (or bisected) CDF/quantile so
# PIT / coverage / quantile-MAE still have an exact reference. Two are exposed as
# factories (nu, n_modes) so Phase 2 can sweep the difficulty knob.
# ---------------------------------------------------------------------------
def _mixture_ppf(cdf, x, q, lo=-30.0, hi=30.0, iters=60):
    """Quantile of a monotone CDF by bisection (mixtures have no closed-form ppf)."""
    lo = np.full(x.shape[0], lo)
    hi = np.full(x.shape[0], hi)
    for _ in range(iters):
        mid = 0.5 * (lo + hi)
        over = cdf(mid, x) >= q
        hi = np.where(over, mid, hi)
        lo = np.where(over, lo, mid)
    return 0.5 * (lo + hi)


def make_heavy_t(nu_lo=3.0, nu_hi=8.0) -> Generator:
    """Location-scale Student-t with x-dependent df in [nu_lo, nu_hi].

    The clean MDN-killer: a finite Gaussian mixture has exponential tails and must burn
    components to fake a polynomial tail, while the NSF's linear spline tails extrapolate.
    Set nu_lo == nu_hi for a constant-df target (Phase 2 sweeps this scalar). Floor at
    nu=3 keeps the variance finite so CRPS is not outlier-dominated (still clearly heavy).
    """

    def df(x):
        return nu_lo + (nu_hi - nu_lo) * _sigmoid(2.0 * x[:, 1])

    def loc(x):
        return 1.0 * x[:, 0]

    def scale(x):
        return 0.3 + 0.3 * _sigmoid(x[:, 1])

    def sample(x, rng):
        return stats.t.rvs(df(x), loc=loc(x), scale=scale(x), random_state=rng)

    def cdf(u, x):
        return stats.t.cdf(u, df(x), loc=loc(x), scale=scale(x))

    def ppf(q, x):
        return stats.t.ppf(q, df(x), loc=loc(x), scale=scale(x))

    tag = f"heavy_t" if nu_lo != nu_hi else f"heavy_t{nu_lo:g}"
    return Generator(tag, sample, cdf, ppf)


def make_many_modes(n_modes=5) -> Generator:
    """Mixture of n_modes Gaussians, evenly spaced, with x-dependent softmax weights.

    When n_modes exceeds the MDN's K, MDN must underfit. Weights depend on x[:,0] so the
    *effective* number of active modes also varies across x.
    """
    centers = np.linspace(-3.0, 3.0, n_modes)
    s = 0.35

    def weights(x):  # (N, n_modes) softmax over linear-in-x logits
        idx = np.arange(n_modes)
        logits = np.outer(x[:, 0], (idx - (n_modes - 1) / 2.0) * 0.8)
        logits = logits - logits.max(axis=1, keepdims=True)
        w = np.exp(logits)
        return w / w.sum(axis=1, keepdims=True)

    def means(x):  # centers shifted by x[:,1]
        return centers[None, :] + 0.3 * x[:, 1][:, None]

    def sample(x, rng):
        w = weights(x)
        mu = means(x)
        pick = np.array([rng.choice(n_modes, p=w[i]) for i in range(x.shape[0])])
        rows = np.arange(x.shape[0])
        return mu[rows, pick] + s * rng.standard_normal(x.shape[0])

    def cdf(u, x):
        w = weights(x)
        mu = means(x)
        u = np.asarray(u).reshape(-1)
        return (w * stats.norm.cdf(u[:, None], mu, s)).sum(axis=1)

    def ppf(q, x):
        return _mixture_ppf(cdf, x, q)

    tag = "many_modes" if n_modes == 5 else f"many_modes{n_modes}"
    return Generator(tag, sample, cdf, ppf)


def make_regime_switch() -> Generator:
    """Discontinuous conditional: unimodal for x[:,0] < 0, bimodal for x[:,0] >= 0.

    Orthogonal failure axis to tail shape — stresses the conditioner network's ability to
    represent a non-smooth p(u|x), not the density family.
    """
    s = 0.4

    def m1(x):
        return -1.6 + 0.3 * x[:, 1]

    def m2(x):
        return 1.6 + 0.3 * x[:, 1]

    def uni_mu(x):
        return 0.5 * x[:, 1]

    def is_bi(x):
        return x[:, 0] >= 0.0

    def cdf(u, x):
        u = np.asarray(u).reshape(-1)
        uni = stats.norm.cdf(u, uni_mu(x), s)
        bi = 0.5 * stats.norm.cdf(u, m1(x), s) + 0.5 * stats.norm.cdf(u, m2(x), s)
        return np.where(is_bi(x), bi, uni)

    def sample(x, rng):
        uni = uni_mu(x) + s * rng.standard_normal(x.shape[0])
        pick = rng.random(x.shape[0]) < 0.5
        bi = np.where(pick, m1(x), m2(x)) + s * rng.standard_normal(x.shape[0])
        return np.where(is_bi(x), bi, uni)

    def ppf(q, x):
        return _mixture_ppf(cdf, x, q)

    return Generator("regime_switch", sample, cdf, ppf)


def make_hetero_skew() -> Generator:
    """Skew-normal with sign-flipping skew alpha(x) and x-dependent scale (H3 both-tails)."""

    def a(x):
        return 6.0 * np.tanh(2.0 * x[:, 0])  # left-skew <-> right-skew across x[:,0]

    def scale(x):
        return 0.3 + 0.4 * np.abs(x[:, 1])

    def sample(x, rng):
        return stats.skewnorm.rvs(a(x), loc=0.0, scale=scale(x), random_state=rng)

    def cdf(u, x):
        return stats.skewnorm.cdf(u, a(x), loc=0.0, scale=scale(x))

    def ppf(q, x):
        return stats.skewnorm.ppf(q, a(x), loc=0.0, scale=scale(x))

    return Generator("hetero_skew", sample, cdf, ppf)


def make_bounded_edge() -> Generator:
    """Truncated normal with a moving hard upper edge (analogue: error near mate scores)."""
    lo = -2.0

    def loc(x):
        return 0.3 * x[:, 1]

    def scale(x):
        return 0.5 + 0.3 * _sigmoid(x[:, 1])

    def hi(x):
        return 0.5 + 0.8 * x[:, 0]  # hard upper boundary that slides with x[:,0]

    def _ab(x):
        return (lo - loc(x)) / scale(x), (hi(x) - loc(x)) / scale(x)

    def sample(x, rng):
        aa, bb = _ab(x)
        return stats.truncnorm.rvs(aa, bb, loc=loc(x), scale=scale(x), random_state=rng)

    def cdf(u, x):
        aa, bb = _ab(x)
        return stats.truncnorm.cdf(u, aa, bb, loc=loc(x), scale=scale(x))

    def ppf(q, x):
        aa, bb = _ab(x)
        return stats.truncnorm.ppf(q, aa, bb, loc=loc(x), scale=scale(x))

    return Generator("bounded_edge", sample, cdf, ppf)


STRESS_GENERATORS = {
    g.name: g
    for g in (
        make_heavy_t(),
        make_many_modes(),
        make_regime_switch(),
        make_hetero_skew(),
        make_bounded_edge(),
        # M>K variants close the unc-001b asterisk: at med, MDN K=5, so only these
        # trigger true MDN underfit (modes > components) — the one regime where the
        # flow could still separate. Run Phase 1 on them to get the full metric block
        # (tail-qMAE + paired deltas), which Phase 2's CRPS-only sweep does not report.
        make_many_modes(8),
        make_many_modes(10),
    )
}
# The four core generators Phase 1 uses by default (bounded_edge is opt-in).
CORE_GENERATORS = ["heavy_t", "many_modes", "regime_switch", "hetero_skew"]


# ---------------------------------------------------------------------------
# Capacity presets — the "fair fight" knob. Each model reads the same trunk width;
# model-specific knobs (flow transforms/bins, MDN K, QR grid) scale together so the
# param counts stay in the same ballpark (reported, not assumed).
# ---------------------------------------------------------------------------
CAPACITY = {
    "small": dict(hidden=(32, 32), transforms=2, bins=6, k=3, levels=19),
    "med": dict(hidden=(64, 64), transforms=3, bins=8, k=5, levels=39),
    "large": dict(hidden=(128, 128), transforms=4, bins=10, k=8, levels=79),
}


def _n_params(module: nn.Module) -> int:
    return int(sum(p.numel() for p in module.parameters()))


class FlowP(Model):
    """Conditional NSF with configurable transforms / bins / trunk width."""

    name = "flow"

    def __init__(self, cap):
        self.flow = zuko.flows.NSF(
            features=1,
            context=CTX_DIM,
            transforms=cap["transforms"],
            bins=cap["bins"],
            hidden_features=cap["hidden"],
        ).to(DEVICE)

    def fit(self, x, u, epochs, batch, lr):
        self.flow.train()
        _train_loop(
            self.flow,
            lambda xb, ub: -self.flow(xb).log_prob(ub).mean(),
            self.flow.parameters(),
            x, u, epochs, batch, lr,
        )
        self.flow.eval()

    @torch.no_grad()
    def cdf(self, u, x):
        d = self.flow(_to_t(x))
        z = d.transform(_to_t(u).reshape(-1, 1)).squeeze(-1).cpu().numpy()
        return stats.norm.cdf(z)

    @torch.no_grad()
    def quantile(self, q, x):
        d = self.flow(_to_t(x))
        zq = torch.full((x.shape[0], 1), float(stats.norm.ppf(q)), device=DEVICE)
        return d.transform.inv(zq).squeeze(-1).cpu().numpy()

    def n_params(self):
        return _n_params(self.flow)


class QRP(Model):
    """Quantile regression on a dense non-crossing grid; configurable width / grid size."""

    name = "qr"

    def __init__(self, cap):
        base = np.linspace(0.005, 0.995, cap["levels"])
        self.levels = np.sort(np.unique(
            np.concatenate([base, EVAL_LEVELS, DEPLOY_Q]).astype(np.float32)
        ))
        h1, h2 = cap["hidden"]
        self.net = nn.Sequential(
            nn.Linear(CTX_DIM, h1), nn.ReLU(),
            nn.Linear(h1, h2), nn.ReLU(),
            nn.Linear(h2, len(self.levels)),
        ).to(DEVICE)
        self._lv = _to_t(self.levels)

    def _predict_grid(self, xt):
        raw = self.net(xt)
        base = raw[:, :1]
        inc = torch.nn.functional.softplus(raw[:, 1:])
        return torch.cat([base, base + torch.cumsum(inc, dim=1)], dim=1)

    def fit(self, x, u, epochs, batch, lr):
        self.net.train()

        def closure(xb, ub):
            pred = self._predict_grid(xb)
            err = ub - pred
            lv = self._lv.unsqueeze(0)
            return torch.maximum(lv * err, (lv - 1.0) * err).mean()

        _train_loop(self.net, closure, self.net.parameters(), x, u, epochs, batch, lr)
        self.net.eval()

    @torch.no_grad()
    def _grid(self, x):
        return self._predict_grid(_to_t(x)).cpu().numpy()

    # NLL is deliberately not implemented: QR's finite-difference density is not a proper
    # score (see unc-001). QR is judged on CRPS / pinball / coverage / quantile-MAE only.
    def cdf(self, u, x):
        g = self._grid(x)
        u = np.asarray(u).reshape(-1)
        return np.array([np.interp(u[i], g[i], self.levels) for i in range(len(u))])

    def quantile(self, q, x):
        g = self._grid(x)
        return np.array([np.interp(q, self.levels, g[i]) for i in range(g.shape[0])])

    def n_params(self):
        return _n_params(self.net)


class MDNP(Model):
    """Mixture density network; Gaussian or Student-t components, configurable K / width.

    The Student-t variant gives the MDN a fighting chance on heavy_t — a fair comparison
    must beat the *best* MDN, not a Gaussian-only strawman.
    """

    def __init__(self, cap, family="gaussian"):
        self.k = cap["k"]
        self.family = family
        self.name = "mdn" if family == "gaussian" else "mdn_t"
        per = 4 if family == "studentt" else 3  # +log-df for Student-t
        h1, h2 = cap["hidden"]
        self.net = nn.Sequential(
            nn.Linear(CTX_DIM, h1), nn.ReLU(),
            nn.Linear(h1, h2), nn.ReLU(),
            nn.Linear(h2, per * self.k),
        ).to(DEVICE)

    def _params(self, xt):
        """Raw mixture parameters. Returns *logits* (not softmaxed) so the NLL can run
        through log_softmax in log-space (see fit); callers needing probabilities
        softmax the logits themselves. The Gaussian sigma floor is raised (exp(-6) ->
        exp(-4)) so a component cannot collapse onto a ~0.0025 spike that NaNs the NLL
        (the unc-001b regime_switch blowup). The Student-t path is left byte-identical.
        """
        out = self.net(xt)
        if self.family == "studentt":
            logits, mu, logsig, logdf = out.chunk(4, dim=1)
            df = torch.nn.functional.softplus(logdf).clamp(1e-3, 98.0) + 2.0
            sig = logsig.clamp(-6, 4).exp()
        else:
            logits, mu, logsig = out.chunk(3, dim=1)
            df = None
            sig = logsig.clamp(-4, 4).exp()
        return logits, mu, sig, df

    def fit(self, x, u, epochs, batch, lr):
        self.net.train()

        def closure(xb, ub):
            logits, mu, sig, df = self._params(xb)
            if self.family == "studentt":
                # Byte-identical to the pre-fix path: log(softmax(logits)).
                comp = torch.distributions.StudentT(df, mu, sig).log_prob(ub)
                log_pi = torch.log(torch.softmax(logits, dim=1))
            else:
                comp = -0.5 * ((ub - mu) / sig) ** 2 - torch.log(
                    sig * math.sqrt(2 * math.pi)
                )
                # log_softmax never materializes a 0 to feed log(), killing the
                # log(0) -> -inf/NaN path that grad-clipping could not rescue.
                log_pi = torch.log_softmax(logits, dim=1)
            return -torch.logsumexp(log_pi + comp, dim=1).mean()

        _train_loop(self.net, closure, self.net.parameters(), x, u, epochs, batch, lr)
        self.net.eval()

    @torch.no_grad()
    def _np_params(self, x):
        logits, mu, sig, df = self._params(_to_t(x))
        pi = torch.softmax(logits, dim=1)
        df = None if df is None else df.cpu().numpy()
        return pi.cpu().numpy(), mu.cpu().numpy(), sig.cpu().numpy(), df

    def _mix_cdf(self, u, pi, mu, sig, df):
        u = np.asarray(u).reshape(-1, 1)
        if df is None:
            comp = stats.norm.cdf(u, mu, sig)
        else:
            comp = stats.t.cdf(u, df, loc=mu, scale=sig)
        return (pi * comp).sum(axis=1)

    def cdf(self, u, x):
        pi, mu, sig, df = self._np_params(x)
        return self._mix_cdf(u, pi, mu, sig, df)

    def quantile(self, q, x):
        pi, mu, sig, df = self._np_params(x)
        lo = np.full(x.shape[0], -40.0)
        hi = np.full(x.shape[0], 40.0)
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            over = self._mix_cdf(mid, pi, mu, sig, df) >= q
            hi = np.where(over, mid, hi)
            lo = np.where(over, lo, mid)
        return 0.5 * (lo + hi)

    def n_params(self):
        return _n_params(self.net)


def build_model(name, cap):
    if name == "flow":
        return FlowP(cap)
    if name == "qr":
        return QRP(cap)
    if name == "mdn":
        return MDNP(cap, family="gaussian")
    if name == "mdn_t":
        return MDNP(cap, family="studentt")
    if name == "unconditional":
        return UnconditionalModel()
    raise ValueError(name)


MODEL_NAMES = ["flow", "mdn", "mdn_t", "qr", "unconditional"]


# ---------------------------------------------------------------------------
# Metrics — deployment-relevant and, crucially, model-agnostic (all computed from
# the common cdf()/quantile() interface, so QR is judged on the same footing).
# ---------------------------------------------------------------------------
def crps(model: Model, x_val, u_val, n_samples=CRPS_SAMPLES):
    """CRPS via the energy form on stratified inverse-CDF samples.

    Drawing q(tau) at stratified tau ~ U(0,1) gives samples from the predicted dist for
    *any* model exposing a quantile function. CRPS = E|X-y| - 0.5 E|X-X'|; the pairwise
    term uses the sorted-sample identity to avoid an (S,S,N) tensor.
    """
    tau = (np.arange(n_samples) + 0.5) / n_samples
    Q = np.stack([model.quantile(float(t), x_val) for t in tau])  # (S, N)
    y = u_val.reshape(-1)
    term1 = np.abs(Q - y[None, :]).mean(axis=0)  # E|X-y|
    a = np.sort(Q, axis=0)  # ascending per column
    k = np.arange(1, n_samples + 1)[:, None]
    coeff = 2 * k - n_samples - 1  # sorted-sample coefficients
    gmm = (coeff * a).sum(axis=0)  # sum_{i<j}(a_j - a_i) per column
    e_xx = (2.0 / n_samples**2) * gmm  # E|X-X'|
    return float(np.mean(term1 - 0.5 * e_xx))


def pinball(model: Model, x_val, u_val, levels):
    y = u_val.reshape(-1)
    out = {}
    for q in levels:
        pred = model.quantile(q, x_val)
        e = y - pred
        out[q] = float(np.mean(np.maximum(q * e, (q - 1.0) * e)))
    return out


def coverage_stratified(model: Model, x_val, u_val, levels, n_bins=N_STRATA):
    """Per-x-bin coverage; returns (worst |emp-nominal| over bins x levels, per-level dict).

    Global coverage hid the unconditional model's failure in unc-001 — stratifying by x is
    the diagnostic that exposes a model that is right on average but wrong per-position.
    """
    y = u_val.reshape(-1)
    edges = np.quantile(x_val[:, 0], np.linspace(0, 1, n_bins + 1))
    edges[0], edges[-1] = -np.inf, np.inf
    bin_id = np.digitize(x_val[:, 0], edges[1:-1])
    worst = 0.0
    per_level = {}
    for q in levels:
        pred = model.quantile(q, x_val)
        hit = (y <= pred).astype(float)
        devs = []
        for b in range(n_bins):
            m = bin_id == b
            if m.any():
                devs.append(abs(hit[m].mean() - q))
        per_level[q] = float(max(devs)) if devs else 0.0
        worst = max(worst, per_level[q])
    return worst, per_level


def pit_ks(model: Model, x_val, u_val):
    pit = np.clip(model.cdf(u_val, x_val), 0.0, 1.0)
    return float(stats.kstest(pit, "uniform").statistic), pit


def quantile_mae(model: Model, gen: Generator, x_val, levels):
    return {
        q: float(np.mean(np.abs(model.quantile(q, x_val) - gen.ppf(q, x_val))))
        for q in levels
    }


def evaluate_stress(model: Model, gen: Generator, x_val, u_val):
    ks, pit = pit_ks(model, x_val, u_val)
    qmae = quantile_mae(model, gen, x_val, EVAL_LEVELS)
    cov_worst, cov_per = coverage_stratified(model, x_val, u_val, EVAL_LEVELS)
    tail_qmae = float(np.mean([qmae[q] for q in TAIL_LEVELS]))
    # Inference latency: time a single per-point quantile read (what a margin would cost).
    t0 = time.perf_counter()
    _ = model.quantile(0.95, x_val)
    infer_us = (time.perf_counter() - t0) / len(u_val) * 1e6
    return {
        "crps": crps(model, x_val, u_val),
        "pit_ks": ks,
        "tail_qmae": tail_qmae,
        "quantile_mae": qmae,
        "pinball": pinball(model, x_val, u_val, DEPLOY_Q),
        "cov_worst": cov_worst,
        "cov_per_level": cov_per,
        "n_params": getattr(model, "n_params", lambda: 0)(),
        "infer_us": infer_us,
        "_pit": pit,
    }


# ---------------------------------------------------------------------------
# Aggregation helpers
# ---------------------------------------------------------------------------
def _mean_ci(vals):
    a = np.asarray(vals, dtype=float)
    m = float(a.mean())
    ci = float(1.96 * a.std(ddof=1) / math.sqrt(len(a))) if len(a) > 1 else 0.0
    return m, ci


def _paired(flow_vals, alt_vals):
    """Paired diff (alt - flow) across seeds: positive => flow better (lower metric)."""
    d = np.asarray(alt_vals) - np.asarray(flow_vals)
    m, ci = _mean_ci(d)
    p = None
    if len(d) >= 6 and np.any(d != 0):
        try:
            p = float(stats.wilcoxon(d).pvalue)
        except ValueError:
            p = None
    return m, ci, p


# ---------------------------------------------------------------------------
# Phase 1 — fair comparison at a fixed capacity (or a capacity sweep)
# ---------------------------------------------------------------------------
def _fit_eval(model_name, cap, gen, seed, args):
    rng = np.random.default_rng(seed)
    torch.manual_seed(seed)
    x_tr, x_val = sample_context(args.n, rng), sample_context(args.n // 4, rng)
    u_tr = gen.sample(x_tr, rng).astype(np.float32)
    u_val = gen.sample(x_val, rng).astype(np.float32)
    model = build_model(model_name, cap)
    t0 = time.perf_counter()
    model.fit(x_tr, u_tr, epochs=args.epochs, batch=args.batch, lr=args.lr)
    train_s = time.perf_counter() - t0
    res = evaluate_stress(model, gen, x_val, u_val)
    res["train_s"] = train_s
    return res


def run_phase1(args):
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    caps = list(CAPACITY) if args.capacity == "sweep" else [args.capacity]
    seeds = list(range(args.seeds))
    summary = {}

    for gen_name in args.generator:
        gen = STRESS_GENERATORS[gen_name]
        print(f"\n=== {gen_name} ===")
        gen_block = {}
        # raw[model][metric] = list over (capacity best-of already folded) -> per-seed
        for model_name in args.model:
            # For a capacity sweep, pick the config with the best mean CRPS (best-tuned).
            best_cap, best_crps, best_runs = None, np.inf, None
            for cap_name in caps:
                cap = CAPACITY[cap_name]
                runs = [_fit_eval(model_name, cap, gen, s, args) for s in seeds]
                mc = np.mean([r["crps"] for r in runs])
                if mc < best_crps:
                    best_cap, best_crps, best_runs = cap_name, mc, runs
            runs = best_runs
            agg = {
                "capacity": best_cap,
                "n_params": int(np.median([r["n_params"] for r in runs])),
                "crps": _mean_ci([r["crps"] for r in runs]),
                "tail_qmae": _mean_ci([r["tail_qmae"] for r in runs]),
                "pit_ks": _mean_ci([r["pit_ks"] for r in runs]),
                "cov_worst": _mean_ci([r["cov_worst"] for r in runs]),
                "infer_us": _mean_ci([r["infer_us"] for r in runs]),
                "train_s": _mean_ci([r["train_s"] for r in runs]),
                "_crps_seeds": [r["crps"] for r in runs],
                "_tqmae_seeds": [r["tail_qmae"] for r in runs],
            }
            gen_block[model_name] = agg
            print(
                f"  {model_name:14s} cap={best_cap:5s} p={agg['n_params']:6d}  "
                f"CRPS={agg['crps'][0]:.4f}±{agg['crps'][1]:.4f}  "
                f"tailqMAE={agg['tail_qmae'][0]:.4f}±{agg['tail_qmae'][1]:.4f}  "
                f"PIT-KS={agg['pit_ks'][0]:.3f}  covWorst={agg['cov_worst'][0]:.3f}  "
                f"{agg['infer_us'][0]:.1f}us/pt"
            )
        summary[gen_name] = gen_block

    _verdict(summary, args)
    _plot_phase1(summary, outdir)
    serial = {
        g: {m: {k: v for k, v in d.items() if not k.startswith("_")}
            for m, d in block.items()}
        for g, block in summary.items()
    }
    (outdir / "phase1_summary.json").write_text(json.dumps(serial, indent=2))
    print(f"\nWrote {outdir / 'phase1_summary.json'}")


def _verdict(summary, args):
    """Apply the pre-registered decision rule and print a verdict."""
    if "flow" not in args.model:
        return
    alts = [m for m in args.model if m in ("mdn", "mdn_t", "qr")]
    print("\n--- Pre-registered H2 verdict (flow vs best alternative) ---")
    # Decisive generators = the flow's best theoretical cases: heavy tails and any
    # many_modes variant present (incl. the M>K underfit runs that close the asterisk).
    decisive_gens = [
        g for g in summary if g == "heavy_t" or g.startswith("many_modes")
    ]
    flow_wins_all = bool(decisive_gens)
    for gen_name in summary:
        block = summary[gen_name]
        if "flow" not in block or not alts:
            continue
        # Best alternative on this generator = lowest mean CRPS among alts.
        alt = min(alts, key=lambda m: block[m]["crps"][0])
        for metric, seedkey in (("crps", "_crps_seeds"), ("tail_qmae", "_tqmae_seeds")):
            m, ci, p = _paired(block["flow"][seedkey], block[alt][seedkey])
            flow_better = m > 0 and (m - ci) > 0  # CI excludes zero in flow's favor
            pstr = f" p={p:.3f}" if p is not None else ""
            tag = "flow<alt" if flow_better else ("~tie" if abs(m) < ci else "alt<flow")
            print(f"  {gen_name:14s} {metric:9s} vs {alt:5s}: "
                  f"Δ(alt-flow)={m:+.4f}±{ci:.4f}{pstr}  [{tag}]")
            if gen_name in decisive_gens and not flow_better:
                flow_wins_all = False
    print(f"\n  VERDICT: {'SHIP FLOW' if flow_wins_all else 'KEEP SIMPLER MODEL (flow -> backstop)'}"
          f"  — flow must beat the best alt on CRPS *and* tail-qMAE on "
          f"{decisive_gens or '(heavy_t, many_modes not run)'} beyond the 95% CI.")


def _plot_phase1(summary, outdir):
    gens = list(summary)
    models = list(next(iter(summary.values())))
    fig, axes = plt.subplots(1, len(gens), figsize=(3.4 * len(gens), 3.4), squeeze=False)
    for ax, g in zip(axes[0], gens):
        block = summary[g]
        xs = [block[m]["crps"][0] for m in models]
        es = [block[m]["crps"][1] for m in models]
        ax.bar(range(len(models)), xs, yerr=es, capsize=3, color="#4C72B0")
        ax.set_xticks(range(len(models)))
        ax.set_xticklabels(models, rotation=45, ha="right", fontsize=8)
        ax.set_title(g, fontsize=9)
        ax.set_ylabel("CRPS (lower=better)")
    fig.suptitle("unc-001b Phase 1 — CRPS by model (mean ± 95% CI)", fontsize=10)
    fig.tight_layout()
    path = outdir / "phase1_crps.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    print(f"  plot -> {path.name}")


# ---------------------------------------------------------------------------
# Phase 2 — crossover: sweep tail-heaviness (nu) and mode-count (M); find where the
# flow overtakes MDN on CRPS. Answers "how hard must the target be for the flow to win."
# ---------------------------------------------------------------------------
def run_phase2(args):
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    cap = CAPACITY[args.capacity if args.capacity != "sweep" else "med"]
    seeds = list(range(args.seeds))
    result = {"nu": {}, "modes": {}}

    def gap(gen, key, val):
        # mean CRPS gap (best MDN - flow); positive => flow wins.
        flow = [_fit_eval("flow", cap, gen, s, args)["crps"] for s in seeds]
        mdn = [_fit_eval("mdn", cap, gen, s, args)["crps"] for s in seeds]
        mdn_t = [_fit_eval("mdn_t", cap, gen, s, args)["crps"] for s in seeds]
        best_mdn = mdn if np.mean(mdn) <= np.mean(mdn_t) else mdn_t
        m, ci, _ = _paired(flow, best_mdn)
        result[key][val] = {"gap": m, "ci": ci, "flow": _mean_ci(flow)[0],
                            "best_mdn": _mean_ci(best_mdn)[0]}
        print(f"  {key}={val:<5} gap(mdn-flow)={m:+.4f}±{ci:.4f}  "
              f"flow={_mean_ci(flow)[0]:.4f} mdn*={_mean_ci(best_mdn)[0]:.4f}")

    print("\n=== Phase 2: tail-heaviness sweep (constant df = nu) ===")
    for nu in args.nu_grid:
        gap(make_heavy_t(nu, nu), "nu", nu)
    print("\n=== Phase 2: mode-count sweep ===")
    for mm in args.modes_grid:
        gap(make_many_modes(int(mm)), "modes", int(mm))

    _plot_crossover(result, outdir)
    (outdir / "phase2_crossover.json").write_text(json.dumps(result, indent=2))
    print(f"\nWrote {outdir / 'phase2_crossover.json'}")


def _plot_crossover(result, outdir):
    fig, axes = plt.subplots(1, 2, figsize=(9, 3.6))
    for ax, (key, xlabel) in zip(axes, [("nu", "Student-t df ν (smaller = heavier tail)"),
                                        ("modes", "number of modes M")]):
        d = result[key]
        xs = sorted(d)
        gaps = [d[x]["gap"] for x in xs]
        cis = [d[x]["ci"] for x in xs]
        ax.axhline(0, color="black", ls=":", lw=1)
        ax.errorbar(xs, gaps, yerr=cis, marker="o", capsize=3, color="#C44E52")
        ax.set_xlabel(xlabel)
        ax.set_ylabel("CRPS gap (MDN* − flow); >0 ⇒ flow wins")
        ax.set_title(f"crossover — {key}")
    fig.suptitle("unc-001b Phase 2 — where does the flow overtake MDN?", fontsize=10)
    fig.tight_layout()
    path = outdir / "phase2_crossover.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    print(f"  plot -> {path.name}")


# ---------------------------------------------------------------------------
def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--phase", choices=["1", "2", "both"], default="1")
    p.add_argument("--generator", nargs="+", default=CORE_GENERATORS,
                   choices=list(STRESS_GENERATORS))
    p.add_argument("--model", nargs="+", default=MODEL_NAMES, choices=MODEL_NAMES)
    p.add_argument("--capacity", choices=["small", "med", "large", "sweep"], default="med")
    p.add_argument("--seeds", type=int, default=5, help="paired seeds (design calls for 10)")
    p.add_argument("--n", type=int, default=20000, help="training samples (val = n/4)")
    p.add_argument("--epochs", type=int, default=150)
    p.add_argument("--batch", type=int, default=1024)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--nu-grid", nargs="+", type=float, default=[3, 4, 6, 10, 30],
                   dest="nu_grid", help="Phase 2 tail-heaviness sweep (df; smaller=heavier)")
    p.add_argument("--modes-grid", nargs="+", type=int, default=[2, 3, 4, 5, 6],
                   dest="modes_grid", help="Phase 2 mode-count sweep")
    p.add_argument("--outdir", default="research/experiment_results/unc-001b")
    args = p.parse_args()

    if args.phase in ("1", "both"):
        run_phase1(args)
    if args.phase in ("2", "both"):
        run_phase2(args)


if __name__ == "__main__":
    main()
