#!/usr/bin/env python3
"""unc-002 uncertainty head trainer: MDN (Student-t) head on a frozen NNUE trunk embedding.

Fits the conditional eval-error distribution p(u | x), where u = v - v_star is the
NNUE static-eval error and x is the frozen trunk embedding. Reports NLL against an
unconditional floor plus calibration (PIT KS, central-interval coverage, pinball
qMAE). See unc_research/experiments/unc-002.md.

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
stale .bin) via unc_research/scripts/prepare_unc_data.py's encoder, so combine_runs.sh output
is enough. unc_research/scripts/prepare_unc_data.py is the one-stop step that combines worker
shards and pre-bakes both .bin ahead of time, but is now optional.
Full flow: datagen -> prepare_unc_data.py -> this trainer.

Each run gets a timestamped dir under unc_research/models/,
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
without retraining. unc_research/scripts/generate_unc_plots.py is the separate analysis tool
(the analogue of scripts/generate_nnue_plots.py): `data` for dataset diagnostics,
`model` for deeper trained-head diagnostics over the val split.

Usage:
  # train (a bare invocation with no subcommand defaults to `train`):
  python3 unc_research/scripts/train_unc_head.py \
      --trunk nnue/model/2026-06-07_00-11-38_61d0444_6.0M_pos/best.bin \
      --train nnue/data/unc_11M/training_data.txt \
      --val   nnue/data/unc_11M/validation_data.txt
  # re-render a past run's figures:
  python3 unc_research/scripts/train_unc_head.py plot unc_research/models/<run>/
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

# Reuse the exact record dtype the preprocessor wrote (prepare_unc_data is a sibling).
sys.path.insert(0, str(Path(__file__).resolve().parent))
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


def _embed_ref(records, ft_w, ft_b):
    """Reference per-row embedding (slow, obviously-correct). Kept as the oracle
    the vectorized embed() is checked against."""
    n = len(records)
    x = np.empty((n, 2 * L1_SIZE), dtype=np.float16)
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


def embed(records, ft_w, ft_b, chunk=20000):
    """Build the 512-dim STM-relative frozen embedding for each record.

    Vectorized over rows in chunks (the per-row Python loop was ~25 min on 10.6M
    positions; this is the reference _embed_ref() batched with a masked gather).
    Padded feature slots are zeroed by the count mask, so the pad value is
    irrelevant. Stored as float16 (features clip to [0,1], so precision loss is
    negligible) to halve the RAM/disk footprint and per-batch bandwidth; the head
    upcasts to fp32 in MDNt.params()."""
    n = len(records)
    x = np.empty((n, 2 * L1_SIZE), dtype=np.float16)
    ftT = np.ascontiguousarray(ft_w.T)      # [HALFKP, L1]; row feat = ftT[feat]
    wi_all = records["white_indices"]
    bi_all = records["black_indices"]
    nw_all = records["num_white"]
    nb_all = records["num_black"]
    stm_all = records["stm"]
    m = wi_all.shape[1]                      # MAX_FEATURES (padded width)
    slot = np.arange(m)
    for s in range(0, n, chunk):
        e = min(s + chunk, n)
        wi = wi_all[s:e].astype(np.int64)    # [C, m]
        bi = bi_all[s:e].astype(np.int64)
        wmask = (slot < nw_all[s:e][:, None])[..., None]  # [C, m, 1]
        bmask = (slot < nb_all[s:e][:, None])[..., None]
        white = np.clip(ft_b + (ftT[wi] * wmask).sum(axis=1), 0.0, 1.0)  # [C, L1]
        black = np.clip(ft_b + (ftT[bi] * bmask).sum(axis=1), 0.0, 1.0)
        wtm = (stm_all[s:e] == 1)[:, None]   # white to move -> [white|black]
        x[s:e, :L1_SIZE] = np.where(wtm, white, black)
        x[s:e, L1_SIZE:] = np.where(wtm, black, white)
    return x


# --------------------------------------------------------------------------- #
#  mdn_t head (Student-t mixture) -- ported from unc001b_stress.py MDNP
# --------------------------------------------------------------------------- #
class ClippedReLU(nn.Module):
    """ReLU clamped to [0, 1] -- QAT: matches the trunk's int8 activation range
    (train_nnue.py's ClippedReLU), so the head's hidden activations train into
    [0,1] -> int8 [0,127] and the fixed-scale int8 export is faithful."""

    def forward(self, x):
        return torch.clamp(x, 0.0, 1.0)


class MDNt(nn.Module):
    def __init__(self, in_dim, k=5, hidden=(128, 128)):
        super().__init__()
        self.k = k
        h1, h2 = hidden
        # ClippedReLU (not ReLU) so the head is quantization-aware like the trunk
        # (unc-008 G). The input embedding is already the trunk's ClippedReLU [0,1].
        self.net = nn.Sequential(
            nn.Linear(in_dim, h1), ClippedReLU(),
            nn.Linear(h1, h2), ClippedReLU(),
            nn.Linear(h2, 4 * k),  # per component: logit, mu, log_sigma, log_df
        )

    def params(self, x):
        raw = self.net(x.float())  # embeddings may be fp16; compute in fp32
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

# QAT fixed-point scales -- identical to the trunk (train_nnue.py): activations
# [0,1] -> int8 [0,127], weights int8 (x64), biases int32 (x8128). The requant
# after a hidden matmul is sum/64 clamped [0,127] (== 127 * ClippedReLU(z)); the
# output layer's int32 accumulator descales by 8128 to raw MDN params.
HEAD_ACT_SCALE = 127
HEAD_WEIGHT_SCALE = 64
HEAD_BIAS_SCALE = HEAD_ACT_SCALE * HEAD_WEIGHT_SCALE  # 8128


def quant_w_i8(w):
    """Float weight -> int8 (x64, clamped [-128,127]). numpy in, numpy int8 out."""
    return np.clip(np.round(np.asarray(w) * HEAD_WEIGHT_SCALE), -128, 127).astype(np.int8)


def quant_b_i32(b):
    """Float bias -> int32 (x8128)."""
    return np.clip(np.round(np.asarray(b) * HEAD_BIAS_SCALE),
                   -(2 ** 31), 2 ** 31 - 1).astype(np.int32)


def int8_head_params(qw, qb, k, x):
    """Integer int8 forward of the QAT head, bit-matching the C++ inference (the
    parity reference). qw/qb are the three layers' int8 weights / int32 biases
    (as stored). x is the float [0,1] embedding [N, in_dim]. Returns numpy
    (log_pi, mu, sigma, df) after the same MDN transforms as MDNt.params.

    Fixed-point contract (mirrors ForwardFromAccumulators): activations are int8
    [0,127] (= round(a*127)); a hidden layer is int32 acc = b_i32 + Wi8.ai8, then
    act = trunc(acc/64) clamped [0,127]; the output layer descales acc by 8128."""
    qx = np.clip(np.round(np.asarray(x, dtype=np.float64) * HEAD_ACT_SCALE),
                 0, 127).astype(np.int32)                       # [N, in_dim]
    a = qx
    for li in (0, 1):  # two ClippedReLU hidden layers
        acc = qb[li][None, :].astype(np.int64) + a.astype(np.int64) @ qw[li].T.astype(np.int64)
        a = np.clip(np.trunc(acc / HEAD_WEIGHT_SCALE), 0, 127).astype(np.int32)
    acc = qb[2][None, :].astype(np.int64) + a.astype(np.int64) @ qw[2].T.astype(np.int64)
    raw = acc.astype(np.float64) / HEAD_BIAS_SCALE              # [N, 4k] raw MDN params
    logits, mu, log_sigma, log_df = np.split(raw, 4, axis=1)
    log_pi = logits - (np.log(np.sum(np.exp(logits - logits.max(1, keepdims=True)),
                                     axis=1, keepdims=True)) + logits.max(1, keepdims=True))
    softplus = lambda z: np.logaddexp(0.0, z)
    sigma = softplus(log_sigma) + 1e-2
    df = np.clip(softplus(log_df), 1e-3, 98.0) + 2.0
    return log_pi, mu, sigma, df


def write_head_bin(path, model, in_dim, k, hidden, u_mean, u_std, trunk_md5, run_id=""):
    """Export the trained MDN head as a flat OZUH binary (the deliverable best.bin).

    Mirrors train_nnue.py's OZNN export and its quantization (unc-008 G): the head
    is QAT (ClippedReLU) so it ships as fixed-scale int8 -- weights int8 (x64),
    biases int32 (x8128) -- run by the C++ integer inference with no runtime
    quantization. The head is meaningless without the frozen trunk that produced
    its embedding and the (u_mean, u_std) the target was standardized with, so both
    are baked in. Reload with read_head_bin().

    Layout (all little-endian):
        4 bytes   magic "OZUH"
        int32     version (=3; QAT int8. v1/v2 were float32, still readable)
        int32     in_dim
        int32     k                       (mixture components)
        int32     n_hidden (=2)
        int32[n_hidden] hidden sizes       (h1, h2)
        float32   u_mean_cp                (de-standardize: u_cp = y*u_std + u_mean)
        float32   u_std_cp
        16 bytes  trunk_md5                (raw; the net that produced the labels)
        int32     run_id_len               (v2+; free-form provenance string)
        bytes     run_id                   (v2+)
        per Linear layer (in->h1, h1->h2, h2->4k):
            v3:      int8[out][in] weight (row-major, x64), int32[out] bias (x8128)
            v1/v2:   float32[out][in] weight (row-major),   float32[out] bias

    v1 (no run_id) is still readable -- read_head_bin() version-gates the field.
    """
    model.eval()
    sd = model.state_dict()
    h = [int(x) for x in hidden]
    run_id_b = run_id.encode("utf-8")
    with open(path, "wb") as f:
        f.write(b"OZUH")
        f.write(struct.pack("<4i", 3, in_dim, k, len(h)))  # v3: QAT int8 weights
        f.write(struct.pack("<%di" % len(h), *h))
        f.write(struct.pack("<2f", float(u_mean), float(u_std)))
        f.write(bytes.fromhex(trunk_md5))
        f.write(struct.pack("<i", len(run_id_b)))
        f.write(run_id_b)
        for wk, bk in _MDNT_LAYER_KEYS:
            w = sd[wk].cpu().numpy()
            b = sd[bk].cpu().numpy()
            sat = float(np.mean(np.abs(w * HEAD_WEIGHT_SCALE) > 127.0))
            if sat > 0.0:
                print(f"  [quant] {wk}: {sat * 100:.3f}% weights saturate int8 "
                      f"(|w|>{127.0 / HEAD_WEIGHT_SCALE:.3f}); max|w|={np.abs(w).max():.3f}")
            f.write(quant_w_i8(w).astype("<i1").tobytes())   # int8 weight (x64)
            f.write(quant_b_i32(b).astype("<i4").tobytes())  # int32 bias (x8128)


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
        run_id = ""
        if version >= 2:
            (run_id_len,) = struct.unpack("<i", f.read(4))
            run_id = f.read(run_id_len).decode("utf-8")
        model = MDNt(in_dim=in_dim, k=k, hidden=tuple(hidden))
        dims = [(hidden[0], in_dim), (hidden[1], hidden[0]), (4 * k, hidden[1])]
        sd = {}
        qw, qb = [], []  # raw int8/int32 arrays (v3) for the exact int8 parity ref
        for (out, inn), (wk, bk) in zip(dims, _MDNT_LAYER_KEYS):
            if version >= 3:  # QAT int8: weight int8 (x64), bias int32 (x8128)
                wi = np.frombuffer(f.read(out * inn), dtype="<i1").reshape(out, inn).copy()
                bi = np.frombuffer(f.read(4 * out), dtype="<i4").copy()
                qw.append(wi.astype(np.int32))
                qb.append(bi.astype(np.int32))
                # dequantized floats for the float MDNt model (analysis convenience)
                w = wi.astype(np.float32) / HEAD_WEIGHT_SCALE
                b = bi.astype(np.float32) / HEAD_BIAS_SCALE
            else:
                w = np.frombuffer(f.read(4 * out * inn), dtype="<f4").reshape(out, inn).copy()
                b = np.frombuffer(f.read(4 * out), dtype="<f4").copy()
            sd[wk] = torch.from_numpy(w)
            sd[bk] = torch.from_numpy(b)
        model.load_state_dict(sd)
    model.eval()
    meta = {"version": version, "in_dim": in_dim, "k": k, "hidden": hidden,
            "u_mean_cp": u_mean, "u_std_cp": u_std, "trunk_md5": trunk_md5,
            "run_id": run_id}
    if version >= 3:  # exact int8 forward inputs for parity (int8_head_params)
        meta["qw"], meta["qb"] = qw, qb
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
def _block_slices(n, bs, block, generator):
    """Yield contiguous (start, end) batch slices in block-shuffled order.

    Full per-row randperm on a 10M-row, 21GB embedding turns every batch into a
    random gather (memory-bandwidth bound). Instead we shuffle the ORDER of large
    contiguous blocks and read fixed slices within them, so xtr[s:e] is a view,
    not a gather. Datagen already de-correlates by game, so block-level shuffling
    is plenty of stochasticity. block is rounded to a multiple of bs so batches
    never straddle a block boundary."""
    block = max(bs, (block // bs) * bs)
    nblocks = (n + block - 1) // block
    for b in torch.randperm(nblocks, generator=generator).tolist():
        b0 = b * block
        b1 = min(b0 + block, n)
        for s in range(b0, b1, bs):
            yield s, min(s + bs, b1)


def _eval_nll(model, x, y, bs, zero_input, in_dim):
    """Mean val NLL, computed in chunks so a 1M-row val set never lands in one
    allocation. zero_input feeds zeros (the unconditional floor) instead of x."""
    model.eval()
    tot, cnt = 0.0, 0
    with torch.no_grad():
        for s in range(0, len(y), bs):
            e = min(s + bs, len(y))
            xb = torch.zeros(e - s, in_dim, dtype=torch.float32) if zero_input else x[s:e]
            tot += model.nll(xb, y[s:e]).item() * (e - s)
            cnt += e - s
    return tot / max(cnt, 1)


def train(model, xtr, ytr, xva, yva, epochs, bs, lr, tag, ckpt_dir=None,
          patience=0, in_dim=None, zero_input=False, shuffle_block=1 << 16,
          warmup_epochs=1, seed=0):
    """Fit the head; if ckpt_dir is given, write a per-epoch checkpoint each epoch.

    patience > 0 enables early stopping (stop after that many epochs with no val
    improvement); patience == 0 runs the full --epochs. zero_input trains on a
    zeroed input (the unconditional floor) without materializing a zeros tensor
    the size of xtr. Uses block-shuffled contiguous batches, a linear LR warmup
    over warmup_epochs, and chunked val. Returns (best_val_nll, history); the
    model is left holding the best-val weights."""
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    n = len(ytr)
    if in_dim is None:
        in_dim = xtr.shape[1]
    gen = torch.Generator().manual_seed(seed)
    steps_per_epoch = max(1, n // bs)
    warmup_steps = max(1, warmup_epochs * steps_per_epoch)
    gstep = 0
    best_val, best_state, bad = float("inf"), None, 0
    history = []  # per-epoch (train_nll, val_nll) for the loss-curve plot
    pbar = tqdm(range(epochs), desc=f"[{tag}]", unit="ep")
    for ep in pbar:
        model.train()
        run_loss = torch.zeros((), dtype=torch.float32)  # accumulate on-tensor
        nb = 0
        for s, e in _block_slices(n, bs, shuffle_block, gen):
            lr_scale = min(1.0, (gstep + 1) / warmup_steps)
            for pg in opt.param_groups:
                pg["lr"] = lr * lr_scale
            xb = torch.zeros(e - s, in_dim, dtype=torch.float32) if zero_input else xtr[s:e]
            opt.zero_grad()
            loss = model.nll(xb, ytr[s:e])
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
            run_loss += loss.detach()
            nb += 1
            gstep += 1
        vnll = _eval_nll(model, xva, yva, max(bs, 4096), zero_input, in_dim)
        train_nll = (run_loss / max(nb, 1)).item()
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


def bin_is_complete(binp):
    """Cheap integrity check for a packed .bin: size must be a whole number of
    records and the LAST record must be populated. A killed encode leaves the
    tail of a pre-sized memmap zero-filled (depth==0, num_white==0), so probing
    the final record catches the truncated-encode case that a mtime check misses
    (the 2026-09-05 98%-zero-filled .bin passed mtime but was truncated)."""
    binp = Path(binp)
    if not binp.exists():
        return False
    itemsize = UNC_RECORD_DTYPE.itemsize
    size = binp.stat().st_size
    if size == 0 or size % itemsize != 0:
        return False
    n = size // itemsize
    last = np.fromfile(binp, dtype=UNC_RECORD_DTYPE, count=1,
                       offset=(n - 1) * itemsize)
    return bool(last["num_white"][0] > 0 and last["depth"][0] > 0)


def ensure_binary_unc(path, label="data"):
    """Resolve a .txt/.bin uncertainty data path to a packed .bin, (re)encoding
    from the .txt whenever the .bin is missing, older than the .txt, or fails the
    completeness check. Mirrors scripts/train_nnue.py::ensure_binary so this
    trainer accepts raw combined .txt directly -- no separate prepare step needed
    (though unc_research/scripts/prepare_unc_data.py still works and pre-bakes the .bin)."""
    p = Path(path)
    if p.suffix == ".bin":
        txt = p.with_suffix(".txt")
        stale = txt.exists() and txt.stat().st_mtime > p.stat().st_mtime
        incomplete = not bin_is_complete(p)
        if txt.exists() and (stale or incomplete):
            why = "source .txt newer" if stale else "cached .bin incomplete/truncated"
            print(f"{why} — re-encoding {label} ...")
            encode_uncertainty(txt, p)
        elif incomplete:
            print(f"WARNING: {p} looks truncated but no .txt to re-encode from")
        else:
            print(f"Using binary {label}: {p}")
        return str(p)
    if p.suffix == ".txt":
        binp = p.with_suffix(".bin")
        if (not binp.exists() or p.stat().st_mtime > binp.stat().st_mtime
                or not bin_is_complete(binp)):
            print(f"Encoding {label} text -> binary ...")
            encode_uncertainty(p, binp)
        else:
            print(f"Using cached binary {label}: {binp}")
        return str(binp)
    return str(p)


# --------------------------------------------------------------------------- #
#  Training/calibration figures (owned by the trainer, like train_nnue.py's
#  generate_plots): rendered at the end of a run and re-renderable from a run's
#  saved artifacts via the `plot` subcommand. generate_unc_plots.py is the
#  separate analysis tool (data + model), the analogue of generate_nnue_plots.py.
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
    trunk_md5 = hashlib.md5(Path(args.trunk).read_bytes()).hexdigest()

    # Embedding cache: 'auto' caches fp16 embeddings next to the training data.
    # The cache key bakes in the trunk md5 and the record count, so a changed net
    # or a re-encoded (different-size) .bin can never silently reuse a stale cache.
    cache_dir = None
    if args.cache == "auto":
        cache_dir = Path(args.train).resolve().parent / ".emb_cache"
    elif args.cache:
        cache_dir = Path(args.cache)

    def load_split(path, name):
        recs = np.fromfile(path, dtype=UNC_RECORD_DTYPE)
        cache = (cache_dir / f"{name}_{trunk_md5[:8]}_{len(recs)}_fp16.npy"
                 if cache_dir else None)
        if cache and cache.exists():
            x = np.load(cache)
            print(f"  {name}: {len(recs)} records (embedding from cache {cache.name})")
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
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    run_dir = Path(args.out) / f"{ts}_{Path(args.trunk).parent.name}_{len(utr)}pos"
    ckpt_dir = run_dir / "checkpoints"
    ckpt_dir.mkdir(parents=True, exist_ok=True)

    # Conditional model (the deliverable) — checkpoints every epoch into ckpt_dir.
    print("\n=== Conditional mdn_t (frozen trunk embedding) ===")
    in_dim = 2 * L1_SIZE
    hidden = tuple(int(x) for x in str(args.hidden).split(","))
    if len(hidden) != 2:
        sys.exit(f"--hidden must be 'h1,h2' (got {args.hidden!r})")
    print(f"head hidden={hidden}  (~{in_dim * hidden[0] + hidden[0] * hidden[1] + hidden[1] * 4 * args.k:,} params)")
    cond = MDNt(in_dim=in_dim, k=args.k, hidden=hidden)
    cond_val, cond_hist = train(cond, xtr_t, ytr_t, xva_t, yva_t, args.epochs, args.bs,
                                args.lr, "cond", ckpt_dir=ckpt_dir, patience=patience,
                                in_dim=in_dim, shuffle_block=args.shuffle_block,
                                warmup_epochs=args.warmup_epochs, seed=args.seed)
    cond_cal = calibration(cond, xva_t, yva_t, u_mean, u_std)

    # Unconditional floor: same head, zeroed input -> learns the marginal p(u).
    # Diagnostic baseline only (not checkpointed): fit on a subsample with
    # zero_input so it never materializes a zeros tensor the size of xtr (that
    # was a second ~21GB allocation) nor grinds all 10.6M rows for a marginal.
    print("\n=== Unconditional floor (zeroed input) ===")
    uncond = MDNt(in_dim=in_dim, k=args.k, hidden=hidden)
    floor_n = min(args.floor_subsample, len(ytr_t))
    fidx = torch.randperm(len(ytr_t), generator=torch.Generator().manual_seed(args.seed))[:floor_n]
    ytr_floor = ytr_t[fidx].contiguous()
    zva = torch.zeros(len(yva_t), in_dim, dtype=torch.float16)  # zero val input, fp16
    print(f"  floor fit on {floor_n:,} subsampled targets (zero input)")
    unc_val, unc_hist = train(uncond, None, ytr_floor, zva, yva_t, args.epochs, args.bs,
                              args.lr, "uncond", patience=patience, in_dim=in_dim,
                              zero_input=True, warmup_epochs=args.warmup_epochs,
                              seed=args.seed)
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
        "epochs": args.epochs, "k": args.k, "hidden": list(hidden), "bs": args.bs, "lr": args.lr,
        "warmup_epochs": args.warmup_epochs, "shuffle_block": args.shuffle_block,
        "floor_subsample": args.floor_subsample,
        "early_stop": bool(args.early_stop), "patience": patience,
        "trunk": str(args.trunk), "trunk_md5": trunk_md5,
    }

    # best.bin = the conditional head at its best-val weights (the deliverable);
    # the unconditional floor is diagnostic. Per-epoch checkpoints are in checkpoints/.
    # run_id = the run-dir name (free-form provenance baked into the OZUH header).
    run_id = run_dir.name
    write_head_bin(run_dir / "best.bin", cond, in_dim=2 * L1_SIZE, k=args.k,
                   hidden=hidden, u_mean=u_mean, u_std=u_std, trunk_md5=trunk_md5,
                   run_id=run_id)
    # Fuse the full trunk + head into a self-contained OZNU deployable next to best.bin
    # (unc-007). Promote it to unc_research/models/nnue_unc.bin manually when a run is
    # chosen, mirroring the best.bin -> nnue/nnue.bin promotion for the eval net.
    import oznu  # local import: oznu imports from this module, so avoid a top-level cycle
    fused, _md5, _rid = oznu.pack_oznu(run_dir / "nnue_unc.bin", args.trunk,
                                       run_dir / "best.bin", run_id)
    print(f"  fused OZNU -> {fused}")
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
    t.add_argument("--hidden", default="128,128",
                   help="head hidden widths 'h1,h2' (default 128,128; small QAT head: 32,32)")
    t.add_argument("--epochs", type=int, default=60)
    t.add_argument("--bs", type=int, default=8192,
                   help="batch size (big is fine: the head is tiny; ~16x fewer steps than 512)")
    t.add_argument("--lr", type=float, default=4e-3,
                   help="Adam LR (sqrt-scaled for bs=8192; linear warmup over --warmup-epochs)")
    t.add_argument("--warmup-epochs", type=float, default=1.0, dest="warmup_epochs",
                   help="linear LR warmup length (epochs) to stabilize the large-batch start")
    t.add_argument("--shuffle-block", type=int, default=1 << 16, dest="shuffle_block",
                   help="block size for block-shuffle; contiguous reads instead of a 21GB random gather")
    t.add_argument("--floor-subsample", type=int, default=1_000_000, dest="floor_subsample",
                   help="rows used to fit the (diagnostic) unconditional floor")
    t.add_argument("--cache", default="auto",
                   help="dir to cache fp16 embeddings (.npy); 'auto' = <train_dir>/.emb_cache, '' = off")
    t.add_argument("--seed", type=int, default=0)
    t.add_argument("--out", default="unc_research/models",
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
    p.add_argument("run", help="unc_research/models/<run>/ (has artifacts.npz)")
    p.set_defaults(func=cmd_plot)

    # Backward-compatible default: bare `train_unc_head.py --trunk ...` runs `train`.
    argv = sys.argv[1:]
    if not argv or (argv[0] not in ("train", "plot") and argv[0] not in ("-h", "--help")):
        argv = ["train"] + argv
    args = ap.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
