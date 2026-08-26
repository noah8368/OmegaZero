#!/usr/bin/env python3
"""Controlled A/B: sparse-ft + SparseAdam  vs  dense-ft + single Adam.

Question: did switching the NNUE feature transformer from sparse gradients
(SparseAdam) to dense gradients (Adam) change generalization -- specifically,
does it overfit earlier? We saw the full dense run's val loss bottom at epoch 2.

Design (isolates the optimizer/gradient change, nothing else):
  * both arms start from IDENTICAL initial weights (same seed, same init draws;
    the sparse arm just re-wraps the same ft.weight in a sparse EmbeddingBag),
  * both see the SAME data subset in the SAME per-epoch order (seeded shuffle),
  * same loss, LR, weight-decay semantics (ft wd=0, dense wd), batch, epochs,
  * both run on CPU so hardware is not a variable (dense is ~3x faster on MPS
    in production; irrelevant to the loss trajectory).

Only differences between arms:
  sparse: EmbeddingBag(sparse=True)  + [SparseAdam(ft), Adam(dense, wd)]
  dense : EmbeddingBag(sparse=False) + [Adam({ft wd0},{dense wd})]

Outputs (results/ab_sparse_vs_dense/):
  ab_history.csv   per-epoch train/val loss for both arms
  ab_curves.png    overlaid val (and train) curves with each arm's val minimum
"""

import argparse
import csv
import importlib.util
import random
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, Subset

REPO = Path(__file__).resolve().parent.parent


def load_train_module():
    path = REPO / "scripts" / "train_nnue.py"
    spec = importlib.util.spec_from_file_location("train_nnue", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def seed_all(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)


def build_model(tn, sparse, seed):
    # Reset RNG so both arms draw the exact same init, then (for the sparse arm)
    # re-wrap the identical ft.weight in a sparse EmbeddingBag.
    seed_all(seed)
    m = tn.NnueNetwork()  # committed default is dense (sparse=False)
    if sparse:
        old = m.ft
        s = nn.EmbeddingBag(old.num_embeddings, old.embedding_dim,
                            mode="sum", sparse=True)
        with torch.no_grad():
            s.weight.copy_(old.weight)
        m.ft = s
    return m


def build_optimizers(model, sparse, lr, wd):
    dense_params = [p for n, p in model.named_parameters() if n != "ft.weight"]
    if sparse:
        return [
            torch.optim.SparseAdam([model.ft.weight], lr=lr),
            torch.optim.Adam(dense_params, lr=lr, weight_decay=wd),
        ]
    return [
        torch.optim.Adam(
            [
                {"params": [model.ft.weight], "weight_decay": 0.0},
                {"params": dense_params, "weight_decay": wd},
            ],
            lr=lr,
        ),
    ]


def run_epoch(tn, model, loader, optimizers, lmbda, device, train, tag=""):
    model.train(train)
    loss_sum = 0.0
    count = 0
    nb = len(loader)
    t0 = time.perf_counter()
    ctx = torch.enable_grad() if train else torch.no_grad()
    with ctx:
        for bi, (w_idx, w_off, b_idx, b_off, score, result, stm) in enumerate(loader):
            w_idx = w_idx.to(device); w_off = w_off.to(device)
            b_idx = b_idx.to(device); b_off = b_off.to(device)
            score = score.to(device); result = result.to(device)
            stm = stm.to(device)
            pred = model(w_idx, w_off, b_idx, b_off, stm).squeeze(1)
            target = tn.compute_target(score, result, stm, lmbda)
            loss = F.mse_loss(torch.sigmoid(pred), target)
            if train:
                for o in optimizers:
                    o.zero_grad()
                loss.backward()
                for o in optimizers:
                    o.step()
            bs = len(w_off)
            loss_sum += loss.item() * bs
            count += bs
            if train and tag and (bi % 100 == 0 or bi == nb - 1):
                el = time.perf_counter() - t0
                rate = (bi + 1) / el
                eta = (nb - bi - 1) / rate if rate > 0 else 0
                print(f"  {tag} batch {bi + 1}/{nb}  {rate:.1f} b/s  "
                      f"eta {eta:5.0f}s", flush=True)
    return loss_sum / max(1, count)


def make_loaders(tn, args, seed):
    train_ds = tn.load_dataset(str(REPO / "nnue/data/100M/training_data.bin"))
    val_ds = tn.load_dataset(str(REPO / "nnue/data/100M/validation_data.bin"))
    n_tr = min(args.n_train, len(train_ds))
    n_va = min(args.n_val, len(val_ds))
    train_sub = Subset(train_ds, range(n_tr))
    val_sub = Subset(val_ds, range(n_va))
    # identical per-epoch shuffle across arms via a fixed-seed generator
    g = torch.Generator().manual_seed(seed)
    train_loader = DataLoader(train_sub, batch_size=args.batch, shuffle=True,
                              generator=g, num_workers=0,
                              collate_fn=tn.binary_collate)
    val_loader = DataLoader(val_sub, batch_size=args.batch, shuffle=False,
                            num_workers=0, collate_fn=tn.binary_collate)
    return train_loader, val_loader, n_tr, n_va


def run_arm(tn, args, sparse, out_rows):
    label = "sparse" if sparse else "dense"
    device = torch.device("cpu")  # both arms on CPU: hardware is not a variable
    torch.set_num_threads(args.threads)
    model = build_model(tn, sparse, args.seed).to(device)
    opts = build_optimizers(model, sparse, args.lr, args.wd)
    # rebuild loaders per arm with the SAME seed -> identical data + order
    train_loader, val_loader, n_tr, n_va = make_loaders(tn, args, args.seed)
    print(f"\n=== arm: {label}  (train={n_tr:,} val={n_va:,}, "
          f"batch={args.batch}, lr={args.lr}, wd={args.wd}, "
          f"epochs={args.epochs}, threads={args.threads}) ===")
    best = (0, float("inf"))
    for ep in range(1, args.epochs + 1):
        t0 = time.perf_counter()
        tr = run_epoch(tn, model, train_loader, opts, args.lmbda, device, True,
                       tag=f"{label} ep{ep}")
        va = run_epoch(tn, model, val_loader, opts, args.lmbda, device, False)
        dt = time.perf_counter() - t0
        if va < best[1]:
            best = (ep, va)
        flag = "  <- val min" if best[0] == ep else ""
        print(f"[{label}] ep {ep:2d}  train {tr:.6f}  val {va:.6f}  "
              f"({dt:5.1f}s){flag}")
        out_rows.append({"arm": label, "epoch": ep, "train_loss": tr,
                         "val_loss": va, "epoch_time_s": round(dt, 1)})
    print(f"[{label}] best val = {best[1]:.6f} at epoch {best[0]}")
    return best


def plot(rows, out_png):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    arms = {}
    for r in rows:
        arms.setdefault(r["arm"], {"ep": [], "tr": [], "va": []})
        arms[r["arm"]]["ep"].append(r["epoch"])
        arms[r["arm"]]["tr"].append(r["train_loss"])
        arms[r["arm"]]["va"].append(r["val_loss"])
    colors = {"sparse": "#4C72B0", "dense": "#DD8452"}
    plt.rcParams.update({"font.size": 11, "axes.grid": True, "grid.alpha": 0.25,
                         "axes.axisbelow": True})
    fig, ax = plt.subplots(figsize=(10, 6.5))
    for name, d in arms.items():
        c = colors.get(name, None)
        ax.plot(d["ep"], d["va"], "-o", color=c, lw=2.2, ms=4,
                label=f"{name} val")
        ax.plot(d["ep"], d["tr"], "--", color=c, lw=1.3, alpha=0.6,
                label=f"{name} train")
        bi = min(range(len(d["va"])), key=lambda i: d["va"][i])
        ax.plot([d["ep"][bi]], [d["va"][bi]], "*", color=c, ms=16, zorder=5)
        ax.annotate(f"{name} val min\nep {d['ep'][bi]}",
                    xy=(d["ep"][bi], d["va"][bi]), fontsize=8, color=c,
                    xytext=(d["ep"][bi] + 0.3, d["va"][bi]))
    ax.set_xlabel("epoch")
    ax.set_ylabel("loss (MSE on sigmoid target)")
    ax.set_title("Sparse (SparseAdam) vs Dense (Adam) feature transformer\n"
                 "identical init + data; solid = val, dashed = train", loc="left")
    ax.legend(loc="upper right", ncol=2, framealpha=0.9)
    fig.tight_layout()
    fig.savefig(out_png, dpi=150, bbox_inches="tight")
    print(f"\nsaved plot -> {out_png}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n-train", type=int, default=20_000_000)
    ap.add_argument("--n-val", type=int, default=2_000_000)
    ap.add_argument("--epochs", type=int, default=10)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--wd", type=float, default=1e-6)
    ap.add_argument("--lmbda", type=float, default=0.7)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--threads", type=int, default=10)
    ap.add_argument("--arms", default="sparse,dense",
                    help="comma list; order = run order")
    args = ap.parse_args()

    tn = load_train_module()
    out_dir = REPO / "results" / "ab_sparse_vs_dense"
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = []
    results = {}
    for arm in args.arms.split(","):
        arm = arm.strip()
        results[arm] = run_arm(tn, args, arm == "sparse", rows)

    with open(out_dir / "ab_history.csv", "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=["arm", "epoch", "train_loss",
                                           "val_loss", "epoch_time_s"])
        wr.writeheader()
        wr.writerows(rows)
    plot(rows, out_dir / "ab_curves.png")

    print("\n=== SUMMARY ===")
    for arm, (ep, va) in results.items():
        print(f"  {arm:6s}: best val {va:.6f} @ epoch {ep}")


if __name__ == "__main__":
    main()
