#!/usr/bin/env python3
"""unc-002 uncertainty head trainer: MDN (Student-t) head on a frozen NNUE trunk embedding.

Fits the conditional eval-error distribution p(u | x), where u = v - v_star is the
NNUE static-eval error and x is the frozen trunk embedding. Reports NLL against an
unconditional floor plus calibration (PIT KS, central-interval coverage, pinball
qMAE). See research/experiments/unc-002.md.

COHERENCE (read this): the result is only meaningful when the labels and the
embedding describe the SAME eval -- i.e. the net that produced the datagen labels
(v, v_star via nnue.bin at datagen time) is the net passed as --trunk here. When
they match, this is a real calibration read; when they differ (e.g. HCE labels vs
an NNUE trunk, or two different NNUE checkpoints), the numbers only verify the
pipeline runs end to end. This script cannot yet detect the datagen net, so it is
the caller's responsibility to pass the matching --trunk (compare its md5 against
the nnue.bin used for datagen).

Embedding recipe (matches scripts/train_nnue.py NnueNetwork.forward exactly):
  own/opp accum[d] = clamp( ft_bias[d] + sum_{active feat} ft_w[d, feat], 0, 1 )
  x = concat([stm_accum, non_stm_accum])            # 512-dim, STM-relative
where ft_w, ft_bias are dequantized from best.bin (int16 / 127).

--train/--val accept either the combined .txt or a pre-encoded .bin: like
scripts/train_nnue.py, this trainer auto-encodes .txt -> .bin (and re-encodes a
stale .bin) via scripts/prepare_unc_data.py's encoder, so combine_runs.sh output
is enough. scripts/prepare_unc_data.py is the one-stop step that combines worker
shards and pre-bakes both .bin ahead of time, but is now optional.
Full flow: datagen -> prepare_unc_data.py -> this trainer.

Each run gets a timestamped dir under research/experiment_results/unc_head/,
mirroring nnue/model:
    <run>/checkpoints/epoch_N.pt   per-epoch checkpoints (gitignored, local only)
    <run>/best.bin                the best-val conditional head (OZUH binary;
                                  in_dim/k/hidden + u_mean/u_std + trunk md5 baked
                                  in, all it needs to reload -- see read_head_bin)
    <run>/metrics.json            calibration + config scalars
    <run>/artifacts.npz           arrays to re-render plots without retraining
    <run>/figs/*.png              calibration/loss plots (unless --no-plots)
Early stopping is off by default (runs the full --epochs); enable with --early-stop.

Like scripts/train_nnue.py, this trainer owns its run figures: `train` renders them
at the end of a run, and `plot <run>` re-renders them from that run's artifacts.npz
without retraining. scripts/generate_unc_head_plots.py is the separate dataset-
analysis tool (the analogue of scripts/generate_nnue_plots.py).

Usage:
  # train (a bare invocation with no subcommand defaults to `train`):
  python3 research/experiments/train_unc_head.py \
      --trunk nnue/model/2026-06-07_00-11-38_61d0444_6.0M_pos/best.bin \
      --train nnue/data/unc_11M/training_data.txt \
      --val   nnue/data/unc_11M/validation_data.txt
  # re-render a past run's figures:
  python3 research/experiments/train_unc_head.py plot research/experiment_results/unc_head/<run>/
"""

import argparse
import hashlib
import json
import struct
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from scipy.stats import t as student_t
from tqdm import tqdm

# Reuse the exact record dtype the preprocessor wrote.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from prepare_unc_data import UNC_RECORD_DTYPE, encode_uncertainty  # noqa: E402

HALFKP_SIZE = 40960
L1_SIZE = 256
FT_SCALE = 127.0

# Consistent, colorblind-friendly roles across every training/calibration figure.
C_COND = "#1f77b4"    # conditional model (blue)
C_FLOOR = "#ff7f0e"   # unconditional floor (orange)
C_REF = "#9E9E9E"     # reference line / ideal


# --------------------------------------------------------------------------- #
#  Frozen trunk: load FT block from best.bin and embed positions
# --------------------------------------------------------------------------- #
def load_ft(trunk_path):
    """Read the feature-transformer weight/bias from a quantized OZNN .bin.

    Returns float32 ft_w [L1_SIZE, HALFKP_SIZE] and ft_b [L1_SIZE], dequantized.
    """
    with open(trunk_path, "rb") as f:
        magic = f.read(4)
        if magic != b"OZNN":
            sys.exit(f"{trunk_path}: bad magic {magic!r} (expected OZNN)")
        halfkp, l1, _l2, _l3 = struct.unpack("<4i", f.read(16))
        if (halfkp, l1) != (HALFKP_SIZE, L1_SIZE):
            sys.exit(f"unexpected trunk dims: {(halfkp, l1)}")
        ft_w = np.frombuffer(f.read(halfkp * l1 * 2), dtype=np.int16)
        ft_w = ft_w.reshape(l1, halfkp).astype(np.float32) / FT_SCALE
        ft_b = np.frombuffer(f.read(l1 * 2), dtype=np.int16).astype(np.float32) / FT_SCALE
    return ft_w, ft_b


def embed(records, ft_w, ft_b):
    """Build the 512-dim STM-relative frozen embedding for each record."""
    n = len(records)
    x = np.empty((n, 2 * L1_SIZE), dtype=np.float32)
    ft_wT = ft_w  # [L1, HALFKP]; column feat = ft_wT[:, feat]
    for i in range(n):
        r = records[i]
        wf = r["white_indices"][: r["num_white"]].astype(np.int64)
        bf = r["black_indices"][: r["num_black"]].astype(np.int64)
        white = np.clip(ft_b + ft_wT[:, wf].sum(axis=1), 0.0, 1.0)
        black = np.clip(ft_b + ft_wT[:, bf].sum(axis=1), 0.0, 1.0)
        if r["stm"] == 1:  # white to move
            x[i] = np.concatenate([white, black])
        else:
            x[i] = np.concatenate([black, white])
    return x


# --------------------------------------------------------------------------- #
#  mdn_t head (Student-t mixture) -- ported from unc001b_stress.py MDNP
# --------------------------------------------------------------------------- #
class MDNt(nn.Module):
    def __init__(self, in_dim, k=5, hidden=(128, 128)):
        super().__init__()
        self.k = k
        h1, h2 = hidden
        self.net = nn.Sequential(
            nn.Linear(in_dim, h1), nn.ReLU(),
            nn.Linear(h1, h2), nn.ReLU(),
            nn.Linear(h2, 4 * k),  # per component: logit, mu, log_sigma, log_df
        )

    def params(self, x):
        raw = self.net(x)
        logits, mu, log_sigma, log_df = raw.chunk(4, dim=1)
        log_pi = torch.log_softmax(logits, dim=1)
        sigma = torch.nn.functional.softplus(log_sigma) + 1e-2  # raised floor
        df = torch.nn.functional.softplus(log_df).clamp(1e-3, 98.0) + 2.0
        return log_pi, mu, sigma, df

    def nll(self, x, y):
        log_pi, mu, sigma, df = self.params(x)
        comp = torch.distributions.StudentT(df, loc=mu, scale=sigma)
        log_prob = comp.log_prob(y.unsqueeze(1))  # [B, K]
        return -torch.logsumexp(log_pi + log_prob, dim=1).mean()


# The three Linear layers of MDNt.net, in forward order (in->h1, h1->h2, h2->4k).
_MDNT_LAYER_KEYS = (("net.0.weight", "net.0.bias"),
                    ("net.2.weight", "net.2.bias"),
                    ("net.4.weight", "net.4.bias"))


def write_head_bin(path, model, in_dim, k, hidden, u_mean, u_std, trunk_md5):
    """Export the trained MDN head as a flat OZUH binary (the deliverable best.bin).

    Mirrors train_nnue.py's OZNN export (magic + int32 dims + raw little-endian
    arrays) but float32 and un-quantized: there is no C++ MDN inference yet to fix
    a quantization scheme against, and quantizing the head's sigma/df params is
    lossy in exactly the tail unc-002 cares about. The head is meaningless without
    the frozen trunk that produced its embedding and the (u_mean, u_std) the target
    was standardized with, so both are baked in. Reload with read_head_bin().

    Layout (all little-endian):
        4 bytes   magic "OZUH"
        int32     version (=1)
        int32     in_dim
        int32     k                       (mixture components)
        int32     n_hidden (=2)
        int32[n_hidden] hidden sizes       (h1, h2)
        float32   u_mean_cp                (de-standardize: u_cp = y*u_std + u_mean)
        float32   u_std_cp
        16 bytes  trunk_md5                (raw; the net that produced the labels)
        per Linear layer (in->h1, h1->h2, h2->4k):
            float32[out][in] weight (row-major), float32[out] bias
    """
    model.eval()
    sd = model.state_dict()
    h = [int(x) for x in hidden]
    with open(path, "wb") as f:
        f.write(b"OZUH")
        f.write(struct.pack("<4i", 1, in_dim, k, len(h)))
        f.write(struct.pack("<%di" % len(h), *h))
        f.write(struct.pack("<2f", float(u_mean), float(u_std)))
        f.write(bytes.fromhex(trunk_md5))
        for wk, bk in _MDNT_LAYER_KEYS:
            f.write(sd[wk].cpu().numpy().astype("<f4").tobytes())
            f.write(sd[bk].cpu().numpy().astype("<f4").tobytes())


def read_head_bin(path):
    """Load an OZUH best.bin written by write_head_bin(). Returns (model, meta).

    The caller must embed positions through the SAME trunk (meta['trunk_md5']) and
    de-standardize predictions with meta['u_mean_cp'/'u_std_cp']."""
    with open(path, "rb") as f:
        if f.read(4) != b"OZUH":
            sys.exit(f"{path}: bad magic (expected OZUH)")
        version, in_dim, k, n_hidden = struct.unpack("<4i", f.read(16))
        hidden = list(struct.unpack("<%di" % n_hidden, f.read(4 * n_hidden)))
        u_mean, u_std = struct.unpack("<2f", f.read(8))
        trunk_md5 = f.read(16).hex()
        model = MDNt(in_dim=in_dim, k=k, hidden=tuple(hidden))
        dims = [(hidden[0], in_dim), (hidden[1], hidden[0]), (4 * k, hidden[1])]
        sd = {}
        for (out, inn), (wk, bk) in zip(dims, _MDNT_LAYER_KEYS):
            w = np.frombuffer(f.read(4 * out * inn), dtype="<f4").reshape(out, inn).copy()
            b = np.frombuffer(f.read(4 * out), dtype="<f4").copy()
            sd[wk] = torch.from_numpy(w)
            sd[bk] = torch.from_numpy(b)
        model.load_state_dict(sd)
    model.eval()
    meta = {"version": version, "in_dim": in_dim, "k": k, "hidden": hidden,
            "u_mean_cp": u_mean, "u_std_cp": u_std, "trunk_md5": trunk_md5}
    return model, meta


def save_checkpoint(ckpt_dir, epoch, model, train_nll, val_nll):
    """Write a per-epoch training checkpoint (state_dict + epoch/loss) to ckpt_dir.

    Lightweight and training-time only (gitignored) — the committed deliverable is
    best.bin, written once from the best-val weights at the end of training."""
    torch.save({"epoch": epoch, "train_nll": train_nll, "val_nll": val_nll,
                "state_dict": model.state_dict()},
               Path(ckpt_dir) / f"epoch_{epoch}.pt")


# --------------------------------------------------------------------------- #
#  Training + calibration diagnostics
# --------------------------------------------------------------------------- #
def train(model, xtr, ytr, xva, yva, epochs, bs, lr, tag, ckpt_dir=None, patience=0):
    """Fit the head; if ckpt_dir is given, write a per-epoch checkpoint each epoch.

    patience > 0 enables early stopping (stop after that many epochs with no val
    improvement); patience == 0 runs the full --epochs. Returns (best_val_nll,
    history). The model is left holding the best-val weights."""
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    n = len(xtr)
    best_val, best_state, bad = float("inf"), None, 0
    history = []  # per-epoch (train_nll, val_nll) for the loss-curve plot
    pbar = tqdm(range(epochs), desc=f"[{tag}]", unit="ep")
    for ep in pbar:
        model.train()
        perm = torch.randperm(n)
        run_loss, nb = 0.0, 0
        for s in range(0, n, bs):
            idx = perm[s : s + bs]
            opt.zero_grad()
            loss = model.nll(xtr[idx], ytr[idx])
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
            run_loss += loss.item()
            nb += 1
        model.eval()
        with torch.no_grad():
            vnll = model.nll(xva, yva).item()
        train_nll = run_loss / max(nb, 1)
        history.append((train_nll, vnll))
        if ckpt_dir is not None:
            save_checkpoint(ckpt_dir, ep, model, train_nll, vnll)
        if vnll < best_val - 1e-4:
            best_val, best_state, bad = vnll, {k: v.clone() for k, v in model.state_dict().items()}, 0
        else:
            bad += 1
        pbar.set_postfix(train=f"{train_nll:.4f}",
                         val=f"{vnll:.4f}", best=f"{best_val:.4f}")
        if patience and bad >= patience:
            pbar.write(f"  [{tag}] early stop at epoch {ep} (no val gain in {patience})")
            break
    pbar.close()
    if best_state is not None:
        model.load_state_dict(best_state)
    return best_val, history


def calibration(model, x, y_cp, u_mean, u_std):
    """PIT, central-interval coverage, and pinball qMAE (all in cp where noted)."""
    model.eval()
    with torch.no_grad():
        log_pi, mu, sigma, df = model.params(x)
    pi = log_pi.exp().numpy()
    mu, sigma, df = mu.numpy(), sigma.numpy(), df.numpy()
    y = y_cp.numpy()  # standardized target values

    # Mixture CDF F(y) = sum_k pi_k * t_cdf((y-mu_k)/sigma_k; df_k)  -> PIT
    def mix_cdf(yv):
        z = (yv[:, None] - mu) / sigma
        return (pi * student_t.cdf(z, df)).sum(axis=1)

    pit = mix_cdf(y)
    # KS distance of PIT vs Uniform(0,1)
    ps = np.sort(pit)
    N = len(ps)
    ks = np.max(np.abs(ps - (np.arange(1, N + 1) / N)))

    # Central coverage at (1-a): fraction with a/2 <= PIT <= 1-a/2 (no inversion needed).
    cov = {}
    for lvl in (0.50, 0.80, 0.90, 0.95):
        a = 1 - lvl
        cov[lvl] = float(((pit >= a / 2) & (pit <= 1 - a / 2)).mean())

    # Pinball / qMAE at a few quantiles: invert mixture CDF by bisection.
    def mix_quantile(tau):
        lo = np.full(N, -50.0)
        hi = np.full(N, 50.0)  # standardized units; +/-50 std brackets everything
        for _ in range(50):
            mid = 0.5 * (lo + hi)
            over = mix_cdf(mid) > tau
            hi = np.where(over, mid, hi)
            lo = np.where(over, lo, mid)
        return 0.5 * (lo + hi)

    pinball = {}
    q_cp = {}  # per-sample predicted quantile, in cp (for the plots)
    for tau in (0.10, 0.50, 0.90):
        q = mix_quantile(tau)
        q_cp[tau] = q * u_std + u_mean
        diff = y - q
        loss = np.where(diff >= 0, tau * diff, (tau - 1) * diff).mean()
        pinball[tau] = float(loss * u_std)  # back to cp

    return dict(ks=float(ks), coverage=cov, pinball_cp=pinball,
                pit=pit, q_cp=q_cp)


def ensure_binary_unc(path, label="data"):
    """Resolve a .txt/.bin uncertainty data path to a packed .bin, (re)encoding
    from the .txt whenever the .bin is missing or older than the .txt. Mirrors
    scripts/train_nnue.py::ensure_binary so this trainer accepts raw combined
    .txt directly -- no separate prepare step needed (though
    scripts/prepare_unc_data.py still works and pre-bakes the .bin)."""
    p = Path(path)
    if p.suffix == ".bin":
        txt = p.with_suffix(".txt")
        if txt.exists() and txt.stat().st_mtime > p.stat().st_mtime:
            print(f"Source .txt newer than .bin — re-encoding {label} ...")
            encode_uncertainty(txt, p)
        else:
            print(f"Using binary {label}: {p}")
        return str(p)
    if p.suffix == ".txt":
        binp = p.with_suffix(".bin")
        if not binp.exists() or p.stat().st_mtime > binp.stat().st_mtime:
            print(f"Encoding {label} text -> binary ...")
            encode_uncertainty(p, binp)
        else:
            print(f"Using cached binary {label}: {binp}")
        return str(binp)
    return str(p)


# --------------------------------------------------------------------------- #
#  Training/calibration figures (owned by the trainer, like train_nnue.py's
#  generate_plots): rendered at the end of a run and re-renderable from a run's
#  saved artifacts via the `plot` subcommand. generate_unc_head_plots.py is the
#  separate dataset-analysis tool, the analogue of generate_nnue_plots.py.
# --------------------------------------------------------------------------- #
def _git_hash():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=Path(__file__).resolve().parents[2],
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"


def _save_plot_metadata(out_dir, command, extra=None):
    meta = {"timestamp": datetime.now().isoformat(), "git_commit": _git_hash(),
            "command": command}
    if extra:
        meta.update(extra)
    (Path(out_dir) / "plot_metadata.json").write_text(json.dumps(meta, indent=2))


def render_head_plots(out_dir, artifacts, meta=None):
    """Render the uncertainty-head figures into out_dir. `artifacts` holds the
    arrays computed during training; see save_head_artifacts() for the schema.
    Called both at the end of a training run and by the `plot` subcommand (from a
    saved artifacts.npz). Best-effort: never raises on a plotting problem."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    levels = [0.50, 0.80, 0.90, 0.95]
    written = []

    # 1. Loss curves: val NLL per epoch, conditional vs floor (train NLL faint).
    fig, ax = plt.subplots(figsize=(9, 6))
    for hist, color, label in ((artifacts["cond_hist"], C_COND, "conditional"),
                               (artifacts["unc_hist"], C_FLOOR, "unconditional floor")):
        hist = np.asarray(hist, dtype=float)
        if hist.size == 0:
            continue
        ep = np.arange(1, len(hist) + 1)
        ax.plot(ep, hist[:, 1], color=color, label=f"{label} (val)")
        ax.plot(ep, hist[:, 0], color=color, alpha=0.35, linestyle="--",
                label=f"{label} (train)")
        best = int(np.argmin(hist[:, 1]))
        ax.scatter([best + 1], [hist[best, 1]], color=color, zorder=5, s=40)
    ax.set_xlabel("epoch")
    ax.set_ylabel("NLL (nats, standardized u)")
    ax.set_title("Uncertainty head — training NLL (lower is better)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    p = out_dir / "head_loss_curves.png"
    fig.savefig(p, dpi=150, bbox_inches="tight")
    plt.close(fig)
    written.append(p)

    # 2. PIT reliability: sorted PIT vs uniform quantiles; on-diagonal = calibrated.
    fig, ax = plt.subplots(figsize=(6.5, 6.5))
    ax.plot([0, 1], [0, 1], color=C_REF, linestyle="--", label="ideal (uniform)")
    for key, color, label, ks in (
            ("cond_pit", C_COND, "conditional", meta and meta.get("cond_ks")),
            ("unc_pit", C_FLOOR, "unconditional floor", meta and meta.get("unc_ks"))):
        pit = np.sort(np.asarray(artifacts[key], dtype=float))
        if pit.size == 0:
            continue
        u = np.arange(1, len(pit) + 1) / len(pit)
        lab = label if ks is None else f"{label} (KS={ks:.3f})"
        ax.plot(pit, u, color=color, label=lab)
    ax.set_xlabel("PIT value")
    ax.set_ylabel("empirical CDF")
    ax.set_title("PIT reliability — deviation from diagonal = miscalibration")
    ax.legend()
    ax.grid(True, alpha=0.3)
    p = out_dir / "head_pit_reliability.png"
    fig.savefig(p, dpi=150, bbox_inches="tight")
    plt.close(fig)
    written.append(p)

    # 3. Coverage calibration: nominal vs empirical central-interval coverage.
    fig, ax = plt.subplots(figsize=(6.5, 6.5))
    ax.plot([0, 1], [0, 1], color=C_REF, linestyle="--", label="ideal")
    for key, color, label in (("cond_coverage", C_COND, "conditional"),
                              ("unc_coverage", C_FLOOR, "unconditional floor")):
        cov = artifacts.get(key)
        if cov is None:
            continue
        emp = np.asarray(cov, dtype=float)  # aligned to `levels`
        ax.plot(levels, emp, "o-", color=color, label=label)
    ax.set_xlabel("nominal central-interval coverage")
    ax.set_ylabel("empirical coverage")
    ax.set_title("Coverage calibration")
    ax.legend()
    ax.grid(True, alpha=0.3)
    p = out_dir / "head_coverage.png"
    fig.savefig(p, dpi=150, bbox_inches="tight")
    plt.close(fig)
    written.append(p)

    # 4. Predicted-uncertainty distribution: per-position 80% interval width (cp).
    #    A spread here = heteroscedasticity (the head assigns position-dependent
    #    uncertainty); the floor is a single constant width for reference.
    q10 = np.asarray(artifacts["cond_q10_cp"], dtype=float)
    q90 = np.asarray(artifacts["cond_q90_cp"], dtype=float)
    if q10.size and q90.size:
        width = np.clip(q90 - q10, 0, np.percentile(q90 - q10, 99.5))
        fig, ax = plt.subplots(figsize=(9, 6))
        ax.hist(width, bins=80, color=C_COND, alpha=0.8, edgecolor="none")
        ax.axvline(float(np.median(width)), color="black", linestyle="--",
                   label=f"median {np.median(width):.0f}cp")
        fw = artifacts.get("unc_width80_cp")
        if fw is not None:
            ax.axvline(float(fw), color=C_FLOOR, linestyle="-",
                       label=f"floor (constant) {float(fw):.0f}cp")
        ax.set_xlabel("predicted central-80% interval width (cp)")
        ax.set_ylabel("positions")
        ax.set_title("Predicted uncertainty per position (heteroscedasticity)")
        ax.legend()
        ax.grid(True, alpha=0.3)
        p = out_dir / "head_uncertainty_distribution.png"
        fig.savefig(p, dpi=150, bbox_inches="tight")
        plt.close(fig)
        written.append(p)

    return written


def save_head_artifacts(out_dir, cond_hist, unc_hist, cond_cal, unc_cal, scalars):
    """Persist everything the head plots need so `train_unc_head.py plot <run>` can
    regenerate them without retraining. Writes artifacts.npz + metrics.json (to the
    run dir; plots render into <run>/figs/)."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    levels = [0.50, 0.80, 0.90, 0.95]
    np.savez(
        out_dir / "artifacts.npz",
        cond_hist=np.asarray(cond_hist, dtype=float),
        unc_hist=np.asarray(unc_hist, dtype=float),
        cond_pit=cond_cal["pit"], unc_pit=unc_cal["pit"],
        cond_coverage=np.array([cond_cal["coverage"][l] for l in levels]),
        unc_coverage=np.array([unc_cal["coverage"][l] for l in levels]),
        cond_q10_cp=cond_cal["q_cp"][0.10], cond_q90_cp=cond_cal["q_cp"][0.90],
        unc_width80_cp=np.array(
            float(np.median(unc_cal["q_cp"][0.90] - unc_cal["q_cp"][0.10]))),
    )
    (out_dir / "metrics.json").write_text(json.dumps(scalars, indent=2))


def _emit_head_plots(run_dir, cond_cal, unc_cal):
    """Render <run>/figs/ from the run's just-saved artifacts.npz + metrics.json,
    so the training-end figures are byte-identical to `train_unc_head.py plot`."""
    data = np.load(run_dir / "artifacts.npz")
    artifacts = {k: data[k] for k in data.files}
    artifacts["unc_width80_cp"] = float(artifacts["unc_width80_cp"])
    figs = run_dir / "figs"
    written = render_head_plots(figs, artifacts,
                                {"cond_ks": cond_cal["ks"], "unc_ks": unc_cal["ks"]})
    _save_plot_metadata(figs, "train", {"plots": [p.name for p in written]})
    return written


def cmd_plot(args):
    """Re-render a past run's calibration/loss figures from its saved artifacts,
    no retraining. The analogue of `train_nnue.py plot --run <run>`."""
    run_dir = Path(args.run)
    npz = run_dir / "artifacts.npz"
    if not npz.exists():
        sys.exit(f"No artifacts.npz in {run_dir} (was the run trained with plots?)")
    data = np.load(npz)
    artifacts = {k: data[k] for k in data.files}
    if "unc_width80_cp" in artifacts:
        artifacts["unc_width80_cp"] = float(artifacts["unc_width80_cp"])
    meta = {}
    mpath = run_dir / "metrics.json"
    if mpath.exists():
        m = json.loads(mpath.read_text())
        meta = {"cond_ks": m.get("cond_ks"), "unc_ks": m.get("unc_ks")}
    figs = run_dir / "figs"
    written = render_head_plots(figs, artifacts, meta)
    _save_plot_metadata(figs, "plot", {"plots": [p.name for p in written]})
    print(f"Wrote {len(written)} head plots to {figs}/")
    for p in written:
        print(f"  {p.name}")


def cmd_train(args):
    patience = args.patience if args.early_stop else 0

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    print(f"Loading trunk {args.trunk} ...")
    ft_w, ft_b = load_ft(args.trunk)

    def load_split(path, name):
        recs = np.fromfile(path, dtype=UNC_RECORD_DTYPE)
        cache = Path(args.cache) / f"{name}_emb.npy" if args.cache else None
        if cache and cache.exists():
            x = np.load(cache)
            print(f"  {name}: {len(recs)} records (embedding from cache)")
        else:
            print(f"  {name}: embedding {len(recs)} records through frozen trunk ...")
            x = embed(recs, ft_w, ft_b)
            if cache:
                cache.parent.mkdir(parents=True, exist_ok=True)
                np.save(cache, x)
        u = recs["u"].astype(np.float32)
        return x, u

    train_bin = ensure_binary_unc(args.train, "train")
    val_bin = ensure_binary_unc(args.val, "val")
    xtr, utr = load_split(train_bin, "train")
    xva, uva = load_split(val_bin, "val")

    # Standardize the target on train stats (PIT/coverage are transform-invariant).
    u_mean, u_std = float(utr.mean()), float(utr.std() + 1e-6)
    print(f"target u: mean={u_mean:.1f}cp  std={u_std:.1f}cp  (n_train={len(utr)})")

    xtr_t = torch.from_numpy(xtr)
    xva_t = torch.from_numpy(xva)
    ytr_t = torch.from_numpy((utr - u_mean) / u_std)
    yva_t = torch.from_numpy((uva - u_mean) / u_std)

    # Run dir (nnue/model-style: <run>/checkpoints/epoch_N.pt + <run>/best.bin) is
    # created up front so the conditional head can checkpoint each epoch into it.
    trunk_md5 = hashlib.md5(Path(args.trunk).read_bytes()).hexdigest()
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    run_dir = Path(args.out) / f"{ts}_{Path(args.trunk).parent.name}_{len(utr)}pos"
    ckpt_dir = run_dir / "checkpoints"
    ckpt_dir.mkdir(parents=True, exist_ok=True)

    # Conditional model (the deliverable) — checkpoints every epoch into ckpt_dir.
    print("\n=== Conditional mdn_t (frozen trunk embedding) ===")
    cond = MDNt(in_dim=2 * L1_SIZE, k=args.k)
    cond_val, cond_hist = train(cond, xtr_t, ytr_t, xva_t, yva_t, args.epochs, args.bs,
                                args.lr, "cond", ckpt_dir=ckpt_dir, patience=patience)
    cond_cal = calibration(cond, xva_t, yva_t, u_mean, u_std)

    # Unconditional floor: same head, zeroed input -> learns the marginal p(u).
    # Diagnostic only, so it is not checkpointed.
    print("\n=== Unconditional floor (zeroed input) ===")
    uncond = MDNt(in_dim=2 * L1_SIZE, k=args.k)
    ztr = torch.zeros_like(xtr_t)
    zva = torch.zeros_like(xva_t)
    unc_val, unc_hist = train(uncond, ztr, ytr_t, zva, yva_t, args.epochs, args.bs,
                              args.lr, "uncond", patience=patience)
    unc_cal = calibration(uncond, zva, yva_t, u_mean, u_std)

    print("\n" + "=" * 62)
    print("unc-002 UNCERTAINTY HEAD RESULTS")
    print("=" * 62)
    print(f"val NLL    conditional {cond_val:.4f}   unconditional {unc_val:.4f}"
          f"   (gain {unc_val - cond_val:+.4f})")
    print(f"PIT KS     conditional {cond_cal['ks']:.4f}   unconditional {unc_cal['ks']:.4f}"
          f"   (lower=better calibrated)")
    print("central-interval coverage (nominal -> empirical):")
    for lvl in (0.50, 0.80, 0.90, 0.95):
        print(f"   {int(lvl*100)}%:  cond {cond_cal['coverage'][lvl]*100:5.1f}%"
              f"   uncond {unc_cal['coverage'][lvl]*100:5.1f}%")
    print("pinball qMAE (cp, lower=better):")
    for tau in (0.10, 0.50, 0.90):
        print(f"   tau={tau:.2f}:  cond {cond_cal['pinball_cp'][tau]:6.1f}"
              f"   uncond {unc_cal['pinball_cp'][tau]:6.1f}")
    print("\nRead: conditioning should lower val NLL & pinball vs the floor; coverage")
    print("near nominal + small PIT KS => the head is calibrated. Valid as a real")
    print("calibration read only if --trunk matches the net used for the datagen")
    print("labels (same nnue.bin); otherwise it only confirms the pipeline runs.")

    # Persist the trained head + metrics ALWAYS (independent of plotting), then
    # emit plots best-effort (a plotting/deps issue must never sink a trained head).
    levels = (0.50, 0.80, 0.90, 0.95)
    taus = (0.10, 0.50, 0.90)
    scalars = {
        "cond_val_nll": cond_val, "unc_val_nll": unc_val,
        "gain": unc_val - cond_val,
        "cond_ks": cond_cal["ks"], "unc_ks": unc_cal["ks"],
        "coverage_nominal": list(levels),
        "cond_coverage": [cond_cal["coverage"][l] for l in levels],
        "unc_coverage": [unc_cal["coverage"][l] for l in levels],
        "pinball_cp_cond": {str(t): cond_cal["pinball_cp"][t] for t in taus},
        "pinball_cp_uncond": {str(t): unc_cal["pinball_cp"][t] for t in taus},
        "n_train": len(utr), "n_val": len(uva),
        "u_mean_cp": u_mean, "u_std_cp": u_std,
        "epochs": args.epochs, "k": args.k, "bs": args.bs, "lr": args.lr,
        "early_stop": bool(args.early_stop), "patience": patience,
        "trunk": str(args.trunk), "trunk_md5": trunk_md5,
    }

    # best.bin = the conditional head at its best-val weights (the deliverable);
    # the unconditional floor is diagnostic. Per-epoch checkpoints are in checkpoints/.
    write_head_bin(run_dir / "best.bin", cond, in_dim=2 * L1_SIZE, k=args.k,
                   hidden=(128, 128), u_mean=u_mean, u_std=u_std, trunk_md5=trunk_md5)
    (run_dir / "metrics.json").write_text(json.dumps(scalars, indent=2))
    n_ckpt = len(list(ckpt_dir.glob("epoch_*.pt")))
    print(f"\nRun dir: {run_dir}")
    print(f"  saved best.bin + metrics.json ({n_ckpt} per-epoch checkpoints in checkpoints/)")

    if not args.no_plots:
        try:
            save_head_artifacts(run_dir, cond_hist, unc_hist, cond_cal, unc_cal,
                                scalars)
            # Render <run>/figs/ from the just-saved artifacts so the training-end
            # figures == `train_unc_head.py plot <run>`.
            written = _emit_head_plots(run_dir, cond_cal, unc_cal)
            print(f"  {len(written)} plots (figs/) + artifacts.npz")
        except Exception as e:  # plotting/deps issue must not sink the run
            print(f"  (plots skipped: {e})")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command")

    # train subcommand (the default when no subcommand is given).
    t = sub.add_parser("train", help="fit the uncertainty head (writes a run dir + plots)")
    t.add_argument("--trunk", default="nnue/nnue.bin")
    t.add_argument("--train", default="nnue/data/unc_11M/training_data.txt",
                   help="training split (.txt or .bin; .txt auto-encodes)")
    t.add_argument("--val", default="nnue/data/unc_11M/validation_data.txt",
                   help="validation split (.txt or .bin; .txt auto-encodes)")
    t.add_argument("--k", type=int, default=5)
    t.add_argument("--epochs", type=int, default=60)
    t.add_argument("--bs", type=int, default=512)
    t.add_argument("--lr", type=float, default=1e-3)
    t.add_argument("--cache", default="", help="dir to cache computed embeddings (.npy)")
    t.add_argument("--seed", type=int, default=0)
    t.add_argument("--out", default="research/experiment_results/unc_head",
                   help="base dir for the timestamped run (best.bin + checkpoints + plots)")
    t.add_argument("--early-stop", action="store_true", dest="early_stop",
                   help="enable early stopping (off by default: run the full --epochs)")
    t.add_argument("--patience", type=int, default=8,
                   help="epochs with no val improvement before early stop (needs --early-stop)")
    t.add_argument("--no-plots", action="store_true", dest="no_plots",
                   help="skip writing training/calibration plots")
    t.set_defaults(func=cmd_train)

    # plot subcommand: re-render a past run's figures (mirrors train_nnue.py plot).
    p = sub.add_parser("plot", help="re-render a run's calibration/loss figures from its artifacts")
    p.add_argument("run", help="research/experiment_results/unc_head/<run>/ (has artifacts.npz)")
    p.set_defaults(func=cmd_plot)

    # Backward-compatible default: bare `train_unc_head.py --trunk ...` runs `train`.
    argv = sys.argv[1:]
    if not argv or (argv[0] not in ("train", "plot") and argv[0] not in ("-h", "--help")):
        argv = ["train"] + argv
    args = ap.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
