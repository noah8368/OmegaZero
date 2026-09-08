#!/usr/bin/env python3
"""Rebuild a head run's training figures WITHOUT retraining, for runs trained
with --no-plots (unc-008 G). Loss curves come from the per-epoch checkpoints
(train/val NLL); the calibration figures come from best.bin re-run on the val
split. The unconditional floor is not persisted, so its comparison line is
omitted (the deliverable conditional head is what these figures document).

Also writes artifacts.npz so `train_unc_head.py plot <run>` works afterwards.

Usage:
    python3 unc_research/scripts/reconstruct_head_plots.py <run_dir> \
        [--val-cache <x.npy>] [--val-bin <validation_data.bin>] [--subsample N]
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from train_unc_head import (calibration, read_head_bin, render_head_plots,  # noqa: E402
                            UNC_RECORD_DTYPE)

LEVELS = (0.50, 0.80, 0.90, 0.95)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--val-cache", default="nnue/data/unc_11M/.emb_cache/"
                    "val_69e1f521_1173730_fp16.npy")
    ap.add_argument("--val-bin", default="nnue/data/unc_11M/validation_data.bin")
    ap.add_argument("--subsample", type=int, default=300000,
                    help="val positions for the calibration figures (0 = all)")
    args = ap.parse_args()

    run_dir = Path(args.run_dir)
    meta_j = json.loads((run_dir / "metrics.json").read_text())
    u_mean, u_std = meta_j["u_mean_cp"], meta_j["u_std_cp"]
    model, _meta = read_head_bin(str(run_dir / "best.bin"))

    # Loss curves from the per-epoch checkpoints (exact training history).
    ckpts = sorted((run_dir / "checkpoints").glob("epoch_*.pt"),
                   key=lambda p: int(p.stem.split("_")[1]))
    hist = []
    for c in ckpts:
        d = torch.load(c, map_location="cpu", weights_only=False)
        hist.append([float(d["train_nll"]), float(d["val_nll"])])
    cond_hist = np.asarray(hist, dtype=float)
    print(f"  loss curve: {len(cond_hist)} epochs from checkpoints")

    # Calibration from best.bin re-run on the val split.
    x = np.load(args.val_cache, mmap_mode="r")
    u = np.fromfile(args.val_bin, dtype=UNC_RECORD_DTYPE)["u"].astype(np.float32)
    n = min(len(x), len(u))
    if args.subsample and args.subsample < n:
        idx = np.random.default_rng(0).choice(n, args.subsample, replace=False)
        idx.sort()
        xv = np.asarray(x[idx], dtype=np.float16)
        uv = u[idx]
    else:
        xv = np.asarray(x[:n], dtype=np.float16)
        uv = u[:n]
    print(f"  calibration on {len(uv):,} val positions")
    cal = calibration(model, torch.from_numpy(xv),
                      torch.from_numpy((uv - u_mean) / u_std), u_mean, u_std)

    # cond-only artifacts (empty/omitted uncond floor -> render skips its series).
    artifacts = {
        "cond_hist": cond_hist,
        "unc_hist": np.zeros((0, 2)),
        "cond_pit": cal["pit"], "unc_pit": np.zeros(0),
        "cond_coverage": np.array([cal["coverage"][l] for l in LEVELS]),
        "cond_q10_cp": cal["q_cp"][0.10], "cond_q90_cp": cal["q_cp"][0.90],
    }
    np.savez(run_dir / "artifacts.npz", **artifacts)
    written = render_head_plots(run_dir / "figs", artifacts,
                                {"cond_ks": cal["ks"], "unc_ks": None})
    print(f"  wrote {len(written)} figs + artifacts.npz -> {run_dir/'figs'}")
    for p in written:
        print(f"    {p}")


if __name__ == "__main__":
    main()
