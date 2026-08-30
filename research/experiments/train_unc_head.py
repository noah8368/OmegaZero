#!/usr/bin/env python3
"""NF-002 uncertainty head trainer: MDN (Student-t) head on a frozen NNUE trunk embedding.

Fits the conditional eval-error distribution p(u | x), where u = v - v_star is the
NNUE static-eval error and x is the frozen trunk embedding. Reports NLL against an
unconditional floor plus calibration (PIT KS, central-interval coverage, pinball
qMAE). See research/experiments/NF-002.md.

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

The trained head plus its metrics are always saved to a timestamped run dir under
research/experiment_results/unc_head/ (head.pt = state_dict + k/hidden/in_dim +
u_mean/u_std + trunk path & md5, everything needed to reload it; reload via
load_head_model()). Training plots (loss curves, calibration/PIT, coverage,
u-distribution) are written alongside unless --no-plots.

Usage:
  python3 research/experiments/train_unc_head.py \
      --trunk nnue/model/2026-06-07_00-11-38_61d0444_6.0M_pos/best.bin \
      --train nnue/data_uncertainty/combined/training_data.txt \
      --val   nnue/data_uncertainty/combined/validation_data.txt
"""

import argparse
import struct
import sys
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
#  mdn_t head (Student-t mixture) -- ported from nf001b_stress.py MDNP
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


def save_head_model(path, model, in_dim, k, hidden, u_mean, u_std, trunk, trunk_md5):
    """Persist the trained MDN head with everything needed to reload and use it.

    The head is meaningless without the frozen trunk that produced its input
    embedding and the (u_mean, u_std) the target was standardized with, so those
    are stored alongside the weights. Reload with load_head_model()."""
    torch.save({
        "arch": "mdn_t",
        "state_dict": model.state_dict(),
        "in_dim": in_dim,
        "k": k,
        "hidden": list(hidden),
        "u_mean_cp": u_mean,     # de-standardize head outputs: u_cp = y * u_std + u_mean
        "u_std_cp": u_std,
        "trunk": str(trunk),     # NNUE .bin whose FT block embeds positions (frozen)
        "trunk_md5": trunk_md5,  # must match the net that produced the datagen labels
    }, path)


def load_head_model(path):
    """Load a head.pt saved by save_head_model(). Returns (model, meta dict).

    The caller must embed positions through the SAME trunk (meta['trunk'] /
    'trunk_md5') and de-standardize predictions with meta['u_mean_cp'/'u_std_cp']."""
    meta = torch.load(path, map_location="cpu", weights_only=False)
    model = MDNt(in_dim=meta["in_dim"], k=meta["k"], hidden=tuple(meta["hidden"]))
    model.load_state_dict(meta["state_dict"])
    model.eval()
    return model, meta


# --------------------------------------------------------------------------- #
#  Training + calibration diagnostics
# --------------------------------------------------------------------------- #
def train(model, xtr, ytr, xva, yva, epochs, bs, lr, tag):
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
        history.append((run_loss / max(nb, 1), vnll))
        if vnll < best_val - 1e-4:
            best_val, best_state, bad = vnll, {k: v.clone() for k, v in model.state_dict().items()}, 0
        else:
            bad += 1
        pbar.set_postfix(train=f"{run_loss / max(nb, 1):.4f}",
                         val=f"{vnll:.4f}", best=f"{best_val:.4f}")
        if bad >= 8:
            pbar.write(f"  [{tag}] early stop at epoch {ep}")
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


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--trunk", default="nnue/nnue.bin")
    ap.add_argument("--train", default="nnue/data_uncertainty/combined/training_data.txt",
                    help="training split (.txt or .bin; .txt auto-encodes)")
    ap.add_argument("--val", default="nnue/data_uncertainty/combined/validation_data.txt",
                    help="validation split (.txt or .bin; .txt auto-encodes)")
    ap.add_argument("--k", type=int, default=5)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--bs", type=int, default=512)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--cache", default="", help="dir to cache computed embeddings (.npy)")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default="research/experiment_results/unc_head",
                    help="base dir for the timestamped run (head.pt + metrics + plots)")
    ap.add_argument("--no-plots", action="store_true", dest="no_plots",
                    help="skip writing training/calibration plots")
    args = ap.parse_args()

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

    # Conditional model.
    print("\n=== Conditional mdn_t (frozen trunk embedding) ===")
    cond = MDNt(in_dim=2 * L1_SIZE, k=args.k)
    cond_val, cond_hist = train(cond, xtr_t, ytr_t, xva_t, yva_t, args.epochs, args.bs, args.lr, "cond")
    cond_cal = calibration(cond, xva_t, yva_t, u_mean, u_std)

    # Unconditional floor: same head, zeroed input -> learns the marginal p(u).
    print("\n=== Unconditional floor (zeroed input) ===")
    uncond = MDNt(in_dim=2 * L1_SIZE, k=args.k)
    ztr = torch.zeros_like(xtr_t)
    zva = torch.zeros_like(xva_t)
    unc_val, unc_hist = train(uncond, ztr, ytr_t, zva, yva_t, args.epochs, args.bs, args.lr, "uncond")
    unc_cal = calibration(uncond, zva, yva_t, u_mean, u_std)

    print("\n" + "=" * 62)
    print("NF-002 UNCERTAINTY HEAD RESULTS")
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
    import hashlib
    import json
    from datetime import datetime

    levels = (0.50, 0.80, 0.90, 0.95)
    taus = (0.10, 0.50, 0.90)
    trunk_md5 = hashlib.md5(Path(args.trunk).read_bytes()).hexdigest()
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
        "trunk": str(args.trunk), "trunk_md5": trunk_md5,
    }
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    run_dir = Path(args.out) / f"{ts}_{Path(args.trunk).parent.name}_{len(utr)}pos"
    run_dir.mkdir(parents=True, exist_ok=True)

    # The conditional head is the deliverable; the unconditional floor is diagnostic.
    save_head_model(run_dir / "head.pt", cond, in_dim=2 * L1_SIZE, k=args.k,
                    hidden=(128, 128), u_mean=u_mean, u_std=u_std,
                    trunk=args.trunk, trunk_md5=trunk_md5)
    (run_dir / "metrics.json").write_text(json.dumps(scalars, indent=2))
    print(f"\nRun dir: {run_dir}")
    print("  saved head.pt + metrics.json")

    if not args.no_plots:
        try:
            from plot_unc import render_head_plots, save_head_artifacts

            save_head_artifacts(run_dir, cond_hist, unc_hist, cond_cal, unc_cal,
                                scalars)
            # Render from the just-saved artifacts so inline == `plot_unc.py head`.
            data = np.load(run_dir / "artifacts.npz")
            artifacts = {k: data[k] for k in data.files}
            artifacts["unc_width80_cp"] = float(artifacts["unc_width80_cp"])
            written = render_head_plots(run_dir, artifacts,
                                        {"cond_ks": cond_cal["ks"],
                                         "unc_ks": unc_cal["ks"]})
            print(f"  {len(written)} plots + artifacts.npz")
        except Exception as e:  # plotting/deps issue must not sink the run
            print(f"  (plots skipped: {e})")


if __name__ == "__main__":
    main()
