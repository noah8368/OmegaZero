#!/usr/bin/env python3
"""NF-001: synthetic validation of conditional-density models + calibration diagnostics.

Before touching chess data, verify that our models (conditional Neural Spline Flow,
quantile regression, mixture density network) and our diagnostics (PIT, coverage,
quantile error) correctly recover *known* conditional distributions p(u | x). This is
the H0 correctness gate and an early read on H2 (which model wins).

See research/experiments/NF-001.md for the design and research/hypotheses.md for H0/H2.

Usage:
    python research/experiments/nf001_synthetic.py \
        --generator hetero_gaussian lognormal bimodal skewnormal \
        --model flow qr mdn unconditional \
        --n 20000 --epochs 150 --seed 0 \
        --outdir research/experiment_results/NF-001

Each (generator, model) pair reports held-out NLL, a PIT KS statistic, coverage at
several quantile levels, and quantile MAE vs the generator's true quantiles. PIT
histograms and coverage plots are written under --outdir.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import torch
import torch.nn as nn
from scipy import stats

import zuko

DEVICE = torch.device("cpu")  # small nets; CPU is plenty and deterministic
CTX_DIM = 2  # dimensionality of the conditioning vector x
QUANTILE_LEVELS = (0.5, 0.8, 0.9, 0.95, 0.99)


# ---------------------------------------------------------------------------
# Synthetic generators: each knows how to sample u | x and its own true CDF /
# quantile, so PIT and coverage have an exact reference to check against.
# ---------------------------------------------------------------------------
@dataclass
class Generator:
    name: str
    sample: Callable[[np.ndarray, np.random.Generator], np.ndarray]  # (x, rng) -> u
    cdf: Callable[[np.ndarray, np.ndarray], np.ndarray]  # (u, x) -> F(u|x)
    ppf: Callable[[float, np.ndarray], np.ndarray]  # (q, x) -> true q-quantile(x)


def _sigmoid(z: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-z))


def make_hetero_gaussian() -> Generator:
    """Signed, heteroscedastic Gaussian — the 'tactical positions are noisier' analogue."""

    def mu(x):
        return 1.0 * x[:, 0]

    def sigma(x):
        return 0.2 + 0.5 * np.abs(x[:, 1])

    def sample(x, rng):
        return mu(x) + sigma(x) * rng.standard_normal(x.shape[0])

    def cdf(u, x):
        return stats.norm.cdf(u, loc=mu(x), scale=sigma(x))

    def ppf(q, x):
        return mu(x) + sigma(x) * stats.norm.ppf(q)

    return Generator("hetero_gaussian", sample, cdf, ppf)


def make_lognormal() -> Generator:
    """Skewed, nonnegative, heavy right tail — matches the |error| shape."""

    def mlog(x):
        return 0.2 * x[:, 0]

    def slog(x):
        return 0.3 + 0.3 * _sigmoid(x[:, 1])

    def sample(x, rng):
        return np.exp(mlog(x) + slog(x) * rng.standard_normal(x.shape[0]))

    def cdf(u, x):
        u = np.clip(u, 1e-9, None)
        return stats.norm.cdf((np.log(u) - mlog(x)) / slog(x))

    def ppf(q, x):
        return np.exp(mlog(x) + slog(x) * stats.norm.ppf(q))

    return Generator("lognormal", sample, cdf, ppf)


def make_bimodal() -> Generator:
    """Two Gaussians with x-dependent mixing weight — single-component models must fail."""
    s1, s2 = 0.4, 0.5

    def w(x):
        return _sigmoid(1.5 * x[:, 0])

    def m1(x):
        return -1.5 + 0.5 * x[:, 1]

    def m2(x):
        return 1.5 + 0.5 * x[:, 1]

    def sample(x, rng):
        pick = rng.random(x.shape[0]) < w(x)
        a = m1(x) + s1 * rng.standard_normal(x.shape[0])
        b = m2(x) + s2 * rng.standard_normal(x.shape[0])
        return np.where(pick, a, b)

    def cdf(u, x):
        return w(x) * stats.norm.cdf(u, m1(x), s1) + (1 - w(x)) * stats.norm.cdf(
            u, m2(x), s2
        )

    def ppf(q, x):
        # No closed form for a mixture quantile; bisect the (monotone) CDF.
        lo = np.full(x.shape[0], -12.0)
        hi = np.full(x.shape[0], 12.0)
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            over = cdf(mid, x) >= q
            hi = np.where(over, mid, hi)
            lo = np.where(over, lo, mid)
        return 0.5 * (lo + hi)

    return Generator("bimodal", sample, cdf, ppf)


def make_skewnormal() -> Generator:
    """Skew-normal — stresses the tail quantiles the search actually uses."""
    a = 4.0  # fixed skewness

    def loc(x):
        return 1.0 * x[:, 0]

    def scale(x):
        return 0.3 + 0.4 * _sigmoid(x[:, 1])

    def sample(x, rng):
        return stats.skewnorm.rvs(
            a, loc=loc(x), scale=scale(x), size=x.shape[0], random_state=rng
        )

    def cdf(u, x):
        return stats.skewnorm.cdf(u, a, loc=loc(x), scale=scale(x))

    def ppf(q, x):
        return stats.skewnorm.ppf(q, a, loc=loc(x), scale=scale(x))

    return Generator("skewnormal", sample, cdf, ppf)


GENERATORS = {
    g.name: g
    for g in (
        make_hetero_gaussian(),
        make_lognormal(),
        make_bimodal(),
        make_skewnormal(),
    )
}


def sample_context(n: int, rng: np.random.Generator) -> np.ndarray:
    return rng.standard_normal((n, CTX_DIM)).astype(np.float32)


# ---------------------------------------------------------------------------
# Models: a common interface so the diagnostics don't care which one they hold.
#   fit(x, u)                 -> train
#   log_prob(u, x) -> (N,)    -> conditional log-density (for held-out NLL)
#   cdf(u, x)      -> (N,)    -> conditional CDF (for PIT)
#   quantile(q, x) -> (N,)    -> conditional q-quantile (for coverage / quantile MAE)
# ---------------------------------------------------------------------------
class Model:
    name: str

    def fit(self, x, u, epochs, batch, lr):
        raise NotImplementedError

    def log_prob(self, u, x):
        raise NotImplementedError

    def cdf(self, u, x):
        raise NotImplementedError

    def quantile(self, q, x):
        raise NotImplementedError


def _to_t(a: np.ndarray) -> torch.Tensor:
    return torch.as_tensor(a, dtype=torch.float32, device=DEVICE)


def _train_loop(module, closure, params, x, u, epochs, batch, lr):
    """Shared minibatch Adam loop. `closure(xb, ub) -> scalar loss`."""
    opt = torch.optim.Adam(params, lr=lr)
    n = x.shape[0]
    xt, ut = _to_t(x), _to_t(u).reshape(-1, 1)
    for _ in range(epochs):
        perm = torch.randperm(n)
        for i in range(0, n, batch):
            idx = perm[i : i + batch]
            opt.zero_grad()
            loss = closure(xt[idx], ut[idx])
            loss.backward()
            opt.step()


class FlowModel(Model):
    """Conditional Neural Spline Flow (zuko). F(u|x)=Phi(T(u;x)); T monotone data->latent."""

    name = "flow"

    def __init__(self):
        self.flow = zuko.flows.NSF(
            features=1, context=CTX_DIM, transforms=3, hidden_features=(64, 64)
        ).to(DEVICE)

    def fit(self, x, u, epochs, batch, lr):
        self.flow.train()
        _train_loop(
            self.flow,
            lambda xb, ub: -self.flow(xb).log_prob(ub).mean(),
            self.flow.parameters(),
            x,
            u,
            epochs,
            batch,
            lr,
        )
        self.flow.eval()

    @torch.no_grad()
    def log_prob(self, u, x):
        return self.flow(_to_t(x)).log_prob(_to_t(u).reshape(-1, 1)).cpu().numpy()

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


class QRModel(Model):
    """Quantile regression: MLP predicts a non-crossing grid of quantiles (pinball loss).

    The trained grid doubles as a piecewise-linear CDF (for PIT) and quantile function.
    """

    name = "qr"
    # Dense internal grid so interpolation to arbitrary levels is accurate.
    LEVELS = np.concatenate(
        [np.linspace(0.01, 0.99, 99), np.array(QUANTILE_LEVELS)]
    )

    def __init__(self):
        self.levels = np.sort(np.unique(self.LEVELS.astype(np.float32)))
        k = len(self.levels)
        self.net = nn.Sequential(
            nn.Linear(CTX_DIM, 64),
            nn.ReLU(),
            nn.Linear(64, 64),
            nn.ReLU(),
            nn.Linear(64, k),  # -> base + cumulative positive increments (non-crossing)
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
            pred = self._predict_grid(xb)  # (B, k)
            err = ub - pred  # (B, k)
            lv = self._lv.unsqueeze(0)
            loss = torch.maximum(lv * err, (lv - 1.0) * err)  # pinball
            return loss.mean()

        _train_loop(self.net, closure, self.net.parameters(), x, u, epochs, batch, lr)
        self.net.eval()

    @torch.no_grad()
    def _grid(self, x):
        return self._predict_grid(_to_t(x)).cpu().numpy()  # (N, k)

    # QR is trained for quantiles, not likelihood; a density recovered from the grid is
    # inherently a derived, approximate quantity. Finite-differencing the *dense* grid
    # spikes wherever adjacent quantiles nearly coincide, which can push NLL absurdly
    # below the oracle. We estimate density on a *coarse* uniform level grid so the
    # bin widths are stable — but treat QR's NLL as indicative only; judge QR on
    # pinball loss / coverage / quantile-MAE (see NF-001.md).
    _COARSE = np.linspace(0.05, 0.95, 19)

    def log_prob(self, u, x):
        g = self._grid(x)  # (N, k) quantile values at self.levels
        u = np.asarray(u).reshape(-1)
        out = np.empty(len(u))
        for i in range(len(u)):
            qc = np.interp(self._COARSE, self.levels, g[i])  # coarse quantile values
            mid = 0.5 * (qc[:-1] + qc[1:])  # bin midpoints in u-space
            dtau = np.diff(self._COARSE)
            dq = np.clip(np.diff(qc), 1e-6, None)
            dens = dtau / dq  # p ≈ dF/du per coarse bin
            out[i] = max(np.interp(u[i], mid, dens), 1e-9)
        return np.log(out)

    def cdf(self, u, x):
        g = self._grid(x)
        u = np.asarray(u).reshape(-1)
        return np.array(
            [np.interp(u[i], g[i], self.levels) for i in range(len(u))]
        )

    def quantile(self, q, x):
        g = self._grid(x)
        return np.array(
            [np.interp(q, self.levels, g[i]) for i in range(g.shape[0])]
        )


class MDNModel(Model):
    """Mixture density network: MLP -> K-component Gaussian mixture. Closed-form CDF."""

    name = "mdn"

    def __init__(self, k=3):
        self.k = k
        self.net = nn.Sequential(
            nn.Linear(CTX_DIM, 64),
            nn.ReLU(),
            nn.Linear(64, 64),
            nn.ReLU(),
            nn.Linear(64, 3 * k),  # logits, means, log-sigmas
        ).to(DEVICE)

    def _params(self, xt):
        out = self.net(xt)
        logits, mu, logsig = out.chunk(3, dim=1)
        return torch.softmax(logits, dim=1), mu, logsig.clamp(-6, 4).exp()

    def fit(self, x, u, epochs, batch, lr):
        self.net.train()

        def closure(xb, ub):
            pi, mu, sig = self._params(xb)
            comp = -0.5 * ((ub - mu) / sig) ** 2 - torch.log(
                sig * math.sqrt(2 * math.pi)
            )
            return -torch.logsumexp(torch.log(pi) + comp, dim=1).mean()

        _train_loop(self.net, closure, self.net.parameters(), x, u, epochs, batch, lr)
        self.net.eval()

    @torch.no_grad()
    def _np_params(self, x):
        pi, mu, sig = self._params(_to_t(x))
        return pi.cpu().numpy(), mu.cpu().numpy(), sig.cpu().numpy()

    def log_prob(self, u, x):
        pi, mu, sig = self._np_params(x)
        u = np.asarray(u).reshape(-1, 1)
        comp = stats.norm.pdf(u, mu, sig)
        return np.log(np.clip((pi * comp).sum(axis=1), 1e-12, None))

    def cdf(self, u, x):
        pi, mu, sig = self._np_params(x)
        u = np.asarray(u).reshape(-1, 1)
        return (pi * stats.norm.cdf(u, mu, sig)).sum(axis=1)

    def quantile(self, q, x):
        pi, mu, sig = self._np_params(x)

        def cdf_at(v):
            return (pi * stats.norm.cdf(v[:, None], mu, sig)).sum(axis=1)

        lo = np.full(x.shape[0], -20.0)
        hi = np.full(x.shape[0], 20.0)
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            over = cdf_at(mid) >= q
            hi = np.where(over, mid, hi)
            lo = np.where(over, lo, mid)
        return 0.5 * (lo + hi)


class UnconditionalModel(Model):
    """Floor: a single Gaussian fit to u, ignoring x. Conditional models must beat this."""

    name = "unconditional"

    def fit(self, x, u, epochs, batch, lr):
        self.mu = float(np.mean(u))
        self.sig = float(np.std(u) + 1e-6)

    def log_prob(self, u, x):
        return stats.norm.logpdf(np.asarray(u).reshape(-1), self.mu, self.sig)

    def cdf(self, u, x):
        return stats.norm.cdf(np.asarray(u).reshape(-1), self.mu, self.sig)

    def quantile(self, q, x):
        return np.full(x.shape[0], stats.norm.ppf(q, self.mu, self.sig))


MODELS = {
    "flow": FlowModel,
    "qr": QRModel,
    "mdn": MDNModel,
    "unconditional": UnconditionalModel,
}


# ---------------------------------------------------------------------------
# Diagnostics
# ---------------------------------------------------------------------------
def evaluate(model: Model, gen: Generator, x_val, u_val):
    nll = float(-np.mean(model.log_prob(u_val, x_val)))

    pit = np.clip(model.cdf(u_val, x_val), 0.0, 1.0)
    ks = float(stats.kstest(pit, "uniform").statistic)

    coverage = {}
    quantile_mae = {}
    for q in QUANTILE_LEVELS:
        pred_q = model.quantile(q, x_val)
        coverage[q] = float(np.mean(u_val.reshape(-1) <= pred_q))
        true_q = gen.ppf(q, x_val)
        quantile_mae[q] = float(np.mean(np.abs(pred_q - true_q)))

    return {"nll": nll, "pit_ks": ks, "coverage": coverage, "quantile_mae": quantile_mae, "_pit": pit}


def true_nll(gen: Generator, x_val, u_val) -> float:
    """Oracle NLL from the generator's own density (finite-difference of its CDF)."""
    eps = 1e-4
    dens = (gen.cdf(u_val + eps, x_val) - gen.cdf(u_val - eps, x_val)) / (2 * eps)
    return float(-np.mean(np.log(np.clip(dens, 1e-12, None))))


def plot_pit(results: dict, gen_name: str, outdir: Path):
    models = list(results.keys())
    fig, axes = plt.subplots(1, len(models), figsize=(3.2 * len(models), 3), squeeze=False)
    for ax, m in zip(axes[0], models):
        ax.hist(results[m]["_pit"], bins=20, range=(0, 1), density=True,
                color="#4C72B0", edgecolor="white")
        ax.axhline(1.0, color="#C44E52", ls="--", lw=1)
        ax.set_title(f"{m}\nKS={results[m]['pit_ks']:.3f}", fontsize=9)
        ax.set_xlim(0, 1)
        ax.set_xlabel("PIT")
    fig.suptitle(f"PIT histograms — {gen_name} (flat = calibrated)", fontsize=10)
    fig.tight_layout()
    path = outdir / f"pit_{gen_name}.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path


def plot_coverage(results: dict, gen_name: str, outdir: Path):
    fig, ax = plt.subplots(figsize=(4, 4))
    qs = list(QUANTILE_LEVELS)
    ax.plot([0, 1], [0, 1], color="black", ls=":", lw=1, label="ideal")
    for m, res in results.items():
        ax.plot(qs, [res["coverage"][q] for q in qs], marker="o", label=m)
    ax.set_xlabel("nominal quantile q")
    ax.set_ylabel("empirical coverage")
    ax.set_title(f"Coverage — {gen_name}")
    ax.legend(fontsize=8)
    fig.tight_layout()
    path = outdir / f"coverage_{gen_name}.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path


# ---------------------------------------------------------------------------
def run(args):
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    summary = {}

    for gen_name in args.generator:
        gen = GENERATORS[gen_name]
        rng = np.random.default_rng(args.seed)
        torch.manual_seed(args.seed)

        # Split by fresh draws (i.i.d. here; the by-game split matters on real data).
        x_tr, x_val = sample_context(args.n, rng), sample_context(args.n // 4, rng)
        u_tr = gen.sample(x_tr, rng).astype(np.float32)
        u_val = gen.sample(x_val, rng).astype(np.float32)

        oracle = true_nll(gen, x_val, u_val)
        print(f"\n=== {gen_name} ===  (oracle NLL = {oracle:.4f})")

        results = {}
        for model_name in args.model:
            model = MODELS[model_name]()
            model.fit(x_tr, u_tr, epochs=args.epochs, batch=args.batch, lr=args.lr)
            res = evaluate(model, gen, x_val, u_val)
            results[model_name] = res
            cov = "  ".join(f"{q}:{res['coverage'][q]:.3f}" for q in QUANTILE_LEVELS)
            print(
                f"  {model_name:14s} NLL={res['nll']:.4f} (Δ{res['nll']-oracle:+.4f})  "
                f"PIT-KS={res['pit_ks']:.3f}  cover[{cov}]"
            )

        pit_path = plot_pit(results, gen_name, outdir)
        cov_path = plot_coverage(results, gen_name, outdir)
        print(f"  plots -> {pit_path.name}, {cov_path.name}")

        summary[gen_name] = {
            "oracle_nll": oracle,
            "models": {
                m: {k: v for k, v in r.items() if not k.startswith("_")}
                for m, r in results.items()
            },
        }

    (outdir / "summary.json").write_text(json.dumps(summary, indent=2))
    print(f"\nWrote {outdir / 'summary.json'}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--generator", nargs="+", default=list(GENERATORS), choices=list(GENERATORS))
    p.add_argument("--model", nargs="+", default=list(MODELS), choices=list(MODELS))
    p.add_argument("--n", type=int, default=20000, help="training samples (val = n/4)")
    p.add_argument("--epochs", type=int, default=150)
    p.add_argument("--batch", type=int, default=1024)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--outdir", default="research/experiment_results/NF-001")
    run(p.parse_args())


if __name__ == "__main__":
    main()
