#!/usr/bin/env python3
"""
Train an NNUE evaluation network for OmegaZero.

Architecture: HalfKP (Half King-Piece)
  - Input: 40960 sparse features per perspective
           (64 king_sq x 10 piece_types x 64 piece_sq)
  - Feature transformer: 40960 -> 256 (shared weights), clipped ReLU
  - Concat white + black perspectives -> 512
  - 512 -> 32 -> 32 -> 1

Training target: blend of sigmoid(search_score) and game outcome,
controlled by the lambda parameter.

Subcommands:
    train   — Train a new NNUE model (default if no subcommand given).
    plot    — Regenerate all plots from a previous training run.

Parameters (train subcommand, CLI args override nnue/config.json):
    --data        Training data path (file or directory). When a directory,
                  resolves to combined/ first, then latest timestamped run.
                  When pointing to a .bin, checks for a newer .txt and
                  re-preprocesses automatically.             (default: nnue/data)
    --val-data    Separate validation data file               (default: auto-detected)
    --epochs      Training epochs                             (default: 200)
    --batch       Batch size                                  (default: 16384)
    --lr          Initial learning rate                       (default: 0.001)
    --wd          Weight decay                                (default: 1e-6)
    --lmbda       Score/result blend weight:
                  lmbda * sigmoid(score/400) + (1-lmbda) * result
                                                              (default: 0.7)
    --val-split   Fraction of data for validation if no
                  separate val file                           (default: 0.05)
    --workers     DataLoader worker processes; -1 = auto
                  (all logical cores)                         (default: -1)
    --prefetch    Batches each worker prefetches ahead        (default: 4)
    --shuffle-block  Block-shuffle memmap data within blocks of
                  N records instead of globally; bounds the
                  page-cache working set for near-RAM datasets.
                  0 = global shuffle             (default: 8000000, ~1 GB)
    --checkpoint-every  Save a per-epoch checkpoint every N
                  epochs; 0 = off (best/final always saved)   (default: 0)
    --patience    Early stop after N epochs with no val
                  improvement; 0 = off (run full --epochs)    (default: 0)
    --min-delta   Min val-loss decrease counting as an
                  improvement for early stopping              (default: 0.0)
    --output      Model checkpoint directory                  (default: nnue/model)

Parameters (plot subcommand):
    --run         Path to a training run directory containing
                  training_history.csv and best.pt.
    --val-data    Validation data for score accuracy plot     (default: auto-detect)
    --batch       Batch size for inference                    (default: 4096)

Data preprocessing:
    .txt files are converted to a memory-mapped .bin format on first run for
    efficient random access with 100M+ positions. The .bin is automatically
    re-generated whenever the .txt is newer (e.g. after combining new data).

Output (run directories named <timestamp>_<hash>_<N>_pos):
    nnue/model/<run>/best.bin              — quantized binary weights (int16/int8)
    nnue/model/<run>/best.pt               — best PyTorch checkpoint (by val loss)
    nnue/model/<run>/epoch_N.pt            — per-epoch checkpoints
    nnue/model/<run>/final.pt              — last epoch checkpoint
    nnue/model/<run>/training_history.csv  — per-epoch loss/lr/timing stats
    nnue/model/<run>/training_meta.json    — run config and summary stats

To use trained weights:
    cp nnue/model/<run>/best.bin nnue/nnue.bin
    make

Usage:
    python3 scripts/train_nnue.py
    python3 scripts/train_nnue.py train --data nnue/data/combined/training_data.txt
    python3 scripts/train_nnue.py train --epochs 300 --lr 0.0003
    python3 scripts/train_nnue.py plot --run nnue/model/<run>
"""

import argparse
import csv
import json
import os
import shutil
import struct
import sys
import time
from datetime import datetime
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader, Sampler
from tqdm import tqdm


# --------------------------------------------------------------------------- #
#  HalfKP feature encoding
# --------------------------------------------------------------------------- #

NUM_KING_SQUARES = 64
NUM_PIECE_TYPES = 10  # P N B R Q (x2 colors, no kings)
NUM_PIECE_SQUARES = 64
HALFKP_SIZE = NUM_KING_SQUARES * NUM_PIECE_TYPES * NUM_PIECE_SQUARES  # 40960

L1_SIZE = 256
L2_SIZE = 32
L3_SIZE = 32
MAX_FEATURES = 32

# Piece chars to piece index (from white's perspective).
# Friendly: P=0 N=1 B=2 R=3 Q=4, Enemy: p=5 n=6 b=7 r=8 q=9
WHITE_PIECE_INDEX = {"P": 0, "N": 1, "B": 2, "R": 3, "Q": 4}
BLACK_PIECE_INDEX = {"p": 5, "n": 6, "b": 7, "r": 8, "q": 9}

SCORE_SCALE = 400.0


def mirror_sq(sq):
    """Flip square vertically (rank mirror): a1<->a8, etc."""
    return sq ^ 56


def halfkp_index(king_sq, piece_index, piece_sq):
    return king_sq * (NUM_PIECE_TYPES * NUM_PIECE_SQUARES) + \
           piece_index * NUM_PIECE_SQUARES + piece_sq


def fen_to_halfkp(fen):
    """Convert a FEN string to two lists of active HalfKP feature indices.

    Returns (white_indices, black_indices) where each is a list of ints
    in [0, 40959].
    """
    parts = fen.split()
    board_str = parts[0]

    pieces = []  # (square, char)
    white_king_sq = None
    black_king_sq = None

    sq = 56  # Start at a8 (rank 8, file a)
    for ch in board_str:
        if ch == "/":
            sq -= 16  # Move to start of next rank down
        elif ch.isdigit():
            sq += int(ch)
        else:
            if ch == "K":
                white_king_sq = sq
            elif ch == "k":
                black_king_sq = sq
            else:
                pieces.append((sq, ch))
            sq += 1

    white_features = []
    black_features = []

    for piece_sq, piece_char in pieces:
        # White king perspective
        if piece_char.isupper():
            pi_white = WHITE_PIECE_INDEX.get(piece_char)
        else:
            pi_white = BLACK_PIECE_INDEX.get(piece_char)

        if pi_white is not None:
            white_features.append(
                halfkp_index(white_king_sq, pi_white, piece_sq)
            )

        # Black king perspective: mirror squares, swap colors
        mirrored_piece_sq = mirror_sq(piece_sq)
        mirrored_king_sq = mirror_sq(black_king_sq)

        if piece_char.isupper():
            # White piece is enemy from black's perspective
            pi_black = BLACK_PIECE_INDEX.get(piece_char.lower())
        else:
            # Black piece is friendly from black's perspective
            pi_black = WHITE_PIECE_INDEX.get(piece_char.upper())

        if pi_black is not None:
            black_features.append(
                halfkp_index(mirrored_king_sq, pi_black, mirrored_piece_sq)
            )

    return white_features, black_features


# --------------------------------------------------------------------------- #
#  Dataset
# --------------------------------------------------------------------------- #

class NnueDataset(Dataset):
    """Loads training data from the text format produced by generate_training_data.py.

    Each line: <fen> | <score> | <result>
    """

    def __init__(self, path):
        self.white_features = []
        self.black_features = []
        self.scores = []
        self.results = []
        self.stms = []  # side to move: 1.0 for white, 0.0 for black

        print(f"Loading data from {path}...")
        t0 = time.time()

        num_lines = sum(1 for _ in open(path))
        with open(path) as f:
            for line in tqdm(f, total=num_lines, desc="Loading", unit="pos"):
                line = line.strip()
                if not line:
                    continue

                parts = line.split(" | ")
                if len(parts) != 3:
                    continue

                fen = parts[0].strip()
                score = float(parts[1].strip())
                result = float(parts[2].strip())

                fen_parts = fen.split()
                stm_is_white = (fen_parts[1] == "w") if len(fen_parts) > 1 else True

                wf, bf = fen_to_halfkp(fen)
                self.white_features.append(wf)
                self.black_features.append(bf)
                self.scores.append(score)
                self.results.append(result)
                self.stms.append(1.0 if stm_is_white else 0.0)

        elapsed = time.time() - t0
        print(f"Loaded {len(self.scores)} positions in {elapsed:.1f}s")

    def __len__(self):
        return len(self.scores)

    def __getitem__(self, idx):
        wf = self.white_features[idx]
        bf = self.black_features[idx]

        white_tensor = torch.zeros(HALFKP_SIZE, dtype=torch.float32)
        black_tensor = torch.zeros(HALFKP_SIZE, dtype=torch.float32)

        for i in wf:
            white_tensor[i] = 1.0
        for i in bf:
            black_tensor[i] = 1.0

        score = self.scores[idx]
        result = self.results[idx]
        stm = self.stms[idx]

        return white_tensor, black_tensor, torch.tensor(score, dtype=torch.float32), \
               torch.tensor(result, dtype=torch.float32), \
               torch.tensor(stm, dtype=torch.float32)


RECORD_DTYPE = np.dtype([
    ("num_white", np.uint8),
    ("white_indices", np.uint16, (MAX_FEATURES,)),
    ("num_black", np.uint8),
    ("black_indices", np.uint16, (MAX_FEATURES,)),
    ("score", np.int16),
    ("result", np.uint8),
    ("stm", np.uint8),
])


class BinaryNnueDataset(Dataset):
    """Memory-mapped binary dataset. Scales to 100M+ positions without OOM."""

    def __init__(self, path):
        self.path = path
        file_size = os.path.getsize(path)
        self.num_records = file_size // RECORD_DTYPE.itemsize
        # Opened lazily (see .data) so each DataLoader worker maps the file in
        # its own process. Mapping eagerly here would, under the 'spawn' start
        # method (macOS default), pickle a materialized copy into every worker.
        self._data = None
        print(f"Loaded {self.num_records:,} positions from {path} (memory-mapped)")

    @property
    def data(self):
        if self._data is None:
            self._data = np.memmap(
                self.path, dtype=RECORD_DTYPE, mode="r",
                shape=(self.num_records,),
            )
        return self._data

    def __getstate__(self):
        # Never ship an open memmap across the process boundary.
        state = self.__dict__.copy()
        state["_data"] = None
        return state

    def __len__(self):
        return self.num_records

    def __getitem__(self, idx):
        # Return the raw fixed-size record; all decode happens vectorized in
        # binary_collate. Keeping per-item work to a bare memmap read is what
        # lets many workers scale.
        return self.data[idx]

    def __getitems__(self, indices):
        # Fast batched path: one fancy-index copy yields the whole batch as a
        # structured (B,) array. PyTorch's fetcher calls this when present,
        # skipping the per-sample Python loop entirely.
        return self.data[indices]


def _worker_init(worker_id):
    """Pin each DataLoader worker to a single thread.

    Workers only do numpy indexing + a tensor wrap, so extra threads there just
    contend with the main process's forward/backward. One thread per worker
    keeps the cores available for compute.
    """
    torch.set_num_threads(1)


class BlockShuffleSampler(Sampler):
    """Locality-preserving shuffle for memory-mapped datasets near/over RAM size.

    Globally shuffling a memmap forces the OS to hold the whole file resident in
    the page cache: when the dataset approaches physical RAM (e.g. a 12.8 GB .bin
    on a 16 GB machine), that evicts everything else — including WindowServer —
    into the compressor and can hang the machine into a watchdog panic.

    This sampler instead sorts its assigned indices by file position, cuts them
    into contiguous blocks of `block_size` records, shuffles the block order, and
    shuffles within each block. Access stays local to ~one block at a time, so the
    resident working set is a block rather than the whole file, while batches still
    see a randomized stream. Bigger blocks mix better but grow the working set;
    a block of a few million records (~hundreds of MB) is a good tradeoff.

    The generator advances across epochs, so each epoch draws a fresh order while
    remaining reproducible from `seed`.
    """

    def __init__(self, indices, block_size, seed=42):
        self.indices = np.sort(np.asarray(indices, dtype=np.int64))
        self.block_size = max(1, int(block_size))
        self.generator = np.random.default_rng(seed)

    def __len__(self):
        return len(self.indices)

    def __iter__(self):
        n = len(self.indices)
        num_blocks = (n + self.block_size - 1) // self.block_size
        for b in self.generator.permutation(num_blocks):
            start = int(b) * self.block_size
            block = self.indices[start:start + self.block_size].copy()
            self.generator.shuffle(block)
            yield from block.tolist()


def binary_collate(batch):
    """Pack a batch of raw records into EmbeddingBag inputs, fully vectorized.

    Accepts either a structured (B,) ndarray (from __getitems__, the fast path)
    or a Python list of records (the per-sample __getitem__ fallback). Produces
    the flat-indices + offsets layout EmbeddingBag expects, identical in meaning
    to the old per-sample loop.
    """
    if not isinstance(batch, np.ndarray):
        batch = np.array(batch, dtype=RECORD_DTYPE)

    nw = batch["num_white"].astype(np.int64)          # (B,)
    nb = batch["num_black"].astype(np.int64)
    cols = np.arange(batch["white_indices"].shape[1])  # (MAX_FEATURES,)

    # Row-major gather of the first num_* entries of each row == concatenation
    # of each sample's active features, exactly what EmbeddingBag consumes.
    w_flat = batch["white_indices"][cols[None, :] < nw[:, None]].astype(np.int64)
    b_flat = batch["black_indices"][cols[None, :] < nb[:, None]].astype(np.int64)

    # Offsets = start index of each bag = exclusive prefix sum of lengths.
    w_off = np.zeros(len(batch), dtype=np.int64)
    b_off = np.zeros(len(batch), dtype=np.int64)
    np.cumsum(nw[:-1], out=w_off[1:])
    np.cumsum(nb[:-1], out=b_off[1:])

    return (torch.from_numpy(w_flat),
            torch.from_numpy(w_off),
            torch.from_numpy(b_flat),
            torch.from_numpy(b_off),
            torch.from_numpy(batch["score"].astype(np.float32)),
            torch.from_numpy(batch["result"].astype(np.float32) / 2.0),
            torch.from_numpy(batch["stm"].astype(np.float32)))


def load_dataset(path):
    """Load a dataset from either .bin (binary) or .txt (text) format."""
    if path.endswith(".bin"):
        return BinaryNnueDataset(path)
    return NnueDataset(path)


def ensure_binary(path, label="data"):
    """Resolve a .txt/.bin data path to a memory-mapped .bin, (re)preprocessing
    from the .txt whenever the .bin is missing or older than the .txt. Returns the
    .bin path. Both training and validation must go through this so they load as
    the same 7-field binary record type -- otherwise a .bin train set and a .txt
    val set produce incompatible record shapes for the shared binary_collate."""
    if path.endswith(".bin"):
        txt_path = path[:-4] + ".txt"
        if Path(txt_path).exists() and \
                Path(txt_path).stat().st_mtime > Path(path).stat().st_mtime:
            print(f"Source .txt is newer than .bin — re-preprocessing {label}...")
            preprocess_to_binary(txt_path, path)
        else:
            print(f"Using binary {label}: {path}")
        return path
    if path.endswith(".txt"):
        bin_path = path[:-4] + ".bin"
        if not Path(bin_path).exists() or \
                Path(path).stat().st_mtime > Path(bin_path).stat().st_mtime:
            print(f"Preprocessing {label} text data to binary format...")
            preprocess_to_binary(path, bin_path)
        else:
            print(f"Using cached binary {label}: {bin_path}")
        return bin_path
    return path


def preprocess_to_binary(txt_path, bin_path):
    """Convert a text training file to binary format for memory-mapped loading."""
    print(f"Counting positions in {txt_path}...")
    num_lines = 0
    with open(txt_path) as f:
        for line in f:
            if line.strip():
                num_lines += 1
    print(f"  {num_lines:,} positions")

    data = np.memmap(bin_path, dtype=RECORD_DTYPE, mode="w+", shape=(num_lines,))

    print("Converting to binary format...")
    idx = 0
    skipped = 0
    with open(txt_path) as f:
        for line in tqdm(f, total=num_lines, desc="Processing", unit="pos"):
            line = line.strip()
            if not line:
                continue
            parts = line.split(" | ")
            if len(parts) != 3:
                skipped += 1
                continue

            fen = parts[0].strip()
            score = float(parts[1].strip())
            result = float(parts[2].strip())
            fen_parts = fen.split()
            stm_is_white = (fen_parts[1] == "w") if len(fen_parts) > 1 else True

            wf, bf = fen_to_halfkp(fen)
            if len(wf) > MAX_FEATURES or len(bf) > MAX_FEATURES:
                skipped += 1
                continue

            record = data[idx]
            record["num_white"] = len(wf)
            record["white_indices"][:len(wf)] = wf
            record["num_black"] = len(bf)
            record["black_indices"][:len(bf)] = bf
            record["score"] = int(np.clip(score, -32768, 32767))
            record["result"] = round(result * 2)
            record["stm"] = 1 if stm_is_white else 0
            idx += 1

    data.flush()

    if idx < num_lines:
        with open(bin_path, "r+b") as f:
            f.truncate(idx * RECORD_DTYPE.itemsize)

    file_size = os.path.getsize(bin_path)
    print(f"  Preprocessed: {idx:,} positions ({skipped:,} skipped, {file_size / 1024 / 1024:.1f} MB)")


# --------------------------------------------------------------------------- #
#  Network
# --------------------------------------------------------------------------- #

class ClippedReLU(nn.Module):
    """ReLU clamped to [0, 1]. Matches int8 quantization range."""

    def forward(self, x):
        return torch.clamp(x, 0.0, 1.0)


class NnueNetwork(nn.Module):
    """HalfKP NNUE network.

    Architecture:
        white_input (40960) --+
                              +--> feature_transform (shared EmbeddingBag) --> ClippedReLU
        black_input (40960) --+

        Concat(white_accum, black_accum) based on side to move
        --> 512 --> Linear(32) --> ClippedReLU
                --> Linear(32) --> ClippedReLU
                --> Linear(1)
    """

    def __init__(self):
        super().__init__()
        # sparse=False: materialize a dense 40960x256 grad each step and update ft
        # with a plain Adam. Counterintuitively this is ~2.4x faster end-to-end than
        # sparse=True + SparseAdam on CPU here (profiled 2026-08-25): SparseAdam's
        # per-step gradient coalesce dominated at ~63% of batch time, far outweighing
        # the cost of the dense update. Dense also runs on MPS (sparse ops don't).
        self.ft = nn.EmbeddingBag(HALFKP_SIZE, L1_SIZE, mode="sum", sparse=False)
        self.ft_bias = nn.Parameter(torch.zeros(L1_SIZE))
        self.l2 = nn.Linear(L1_SIZE * 2, L2_SIZE)
        self.l3 = nn.Linear(L2_SIZE, L3_SIZE)
        self.output = nn.Linear(L3_SIZE, 1)
        self.clipped_relu = ClippedReLU()

        self._init_weights()

    def _init_weights(self):
        nn.init.kaiming_normal_(self.ft.weight, nonlinearity="relu")
        nn.init.kaiming_normal_(self.l2.weight, nonlinearity="relu")
        nn.init.zeros_(self.l2.bias)
        nn.init.kaiming_normal_(self.l3.weight, nonlinearity="relu")
        nn.init.zeros_(self.l3.bias)
        nn.init.xavier_normal_(self.output.weight)
        nn.init.zeros_(self.output.bias)

    def forward(self, white_idx, white_off, black_idx, black_off, stm):
        white_accum = self.clipped_relu(self.ft(white_idx, white_off) + self.ft_bias)
        black_accum = self.clipped_relu(self.ft(black_idx, black_off) + self.ft_bias)

        stm_expanded = stm.unsqueeze(1)
        stm_first = stm_expanded * white_accum + (1 - stm_expanded) * black_accum
        stm_second = stm_expanded * black_accum + (1 - stm_expanded) * white_accum
        combined = torch.cat([stm_first, stm_second], dim=1)

        x = self.clipped_relu(self.l2(combined))
        x = self.clipped_relu(self.l3(x))
        return self.output(x)


# --------------------------------------------------------------------------- #
#  Training
# --------------------------------------------------------------------------- #

def compute_target(score, result, lmbda):
    """Blend sigmoid(score) with game result.

    target = lambda * sigmoid(score / SCORE_SCALE) + (1 - lambda) * result
    """
    score_sigmoid = torch.sigmoid(score / SCORE_SCALE)
    return lmbda * score_sigmoid + (1 - lmbda) * result


def generate_plots(train_losses, val_losses, plot_dir,
                    model, val_loader, device, lmbda):
    """Generate and save training plots."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plot_dir = Path(plot_dir)
    plot_dir.mkdir(parents=True, exist_ok=True)
    epochs = range(1, len(train_losses) + 1)

    import numpy as np

    has_val_data = val_loader is not None
    all_predicted = []
    all_actual = []
    all_results = []
    all_material = []

    if has_val_data:
        model.eval()
        with torch.no_grad():
            for batch in val_loader:
                if len(batch) == 7:
                    w_idx, w_off, b_idx, b_off, score, result, stm = batch
                    w_idx = w_idx.to(device)
                    w_off = w_off.to(device)
                    b_idx = b_idx.to(device)
                    b_off = b_off.to(device)
                    stm = stm.to(device)
                    pred = model(w_idx, w_off, b_idx, b_off, stm).squeeze(1)
                    num_features = len(w_idx) / max(len(w_off), 1)
                    all_material.extend([num_features] * len(w_off))
                else:
                    wf, bf, score, result, stm = batch
                    wf = wf.to(device)
                    bf = bf.to(device)
                    stm = stm.to(device)
                    pred = model(wf, bf, stm).squeeze(1)
                    all_material.extend(wf.sum(dim=1).tolist())

                pred_cp = pred * SCORE_SCALE
                all_predicted.extend(pred_cp.cpu().tolist())
                all_actual.extend(score.tolist())
                all_results.extend(result.tolist())

    # Loss curve + score accuracy (side-by-side if val data, loss-only otherwise)
    best_epoch = val_losses.index(min(val_losses)) + 1

    if has_val_data and all_predicted:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    else:
        fig, ax1 = plt.subplots(figsize=(8, 5))

    ax1.plot(epochs, train_losses, label="Train")
    ax1.plot(epochs, val_losses, label="Validation")
    ax1.axvline(best_epoch, color="gray", linestyle="--", alpha=0.5,
                label=f"Best val (epoch {best_epoch})")
    ax1.set_xlabel("Epoch")
    ax1.set_ylabel("Loss [MSE]")
    ax1.set_title("Training Loss")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    if has_val_data and all_predicted:
        ax2.scatter(all_actual, all_predicted, alpha=0.15, s=4)
        lo = min(min(all_actual), min(all_predicted))
        hi = max(max(all_actual), max(all_predicted))
        ax2.plot([lo, hi], [lo, hi], "r--", linewidth=1, label="Perfect prediction")
        ax2.set_xlabel("Actual Score [cp]")
        ax2.set_ylabel("Predicted Score [cp]")
        ax2.set_title("Score Accuracy (Validation)")
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        ax2.set_aspect("equal")

    fig.tight_layout()
    fig.savefig(plot_dir / "loss_and_accuracy.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    if not has_val_data or not all_predicted:
        print(f"  Plots saved to: {plot_dir}")
        print("  (Score accuracy and model evaluation plots skipped — no validation data)")
        return

    # Score distribution: predicted vs actual histograms
    fig, ax = plt.subplots(figsize=(10, 6))
    clip = 2000
    clipped_actual = [max(-clip, min(clip, s)) for s in all_actual]
    clipped_predicted = [max(-clip, min(clip, s)) for s in all_predicted]
    bins = 80
    ax.hist(clipped_actual, bins=bins, alpha=0.5, label="Actual", density=True)
    ax.hist(clipped_predicted, bins=bins, alpha=0.5, label="Predicted", density=True)
    ax.set_xlabel("Score [cp]")
    ax.set_ylabel("Density")
    ax.set_title("Score Distribution (Validation)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(plot_dir / "score_distribution.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Error by game phase: prediction error vs piece count
    errors = [abs(p - a) for p, a in zip(all_predicted, all_actual)]
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.scatter(all_material, errors, alpha=0.1, s=4)
    mat_arr = np.array(all_material)
    err_arr = np.array(errors)
    bin_edges = np.linspace(mat_arr.min(), mat_arr.max(), 20)
    bin_centers = []
    bin_means = []
    for i in range(len(bin_edges) - 1):
        mask = (mat_arr >= bin_edges[i]) & (mat_arr < bin_edges[i + 1])
        if mask.sum() > 0:
            bin_centers.append((bin_edges[i] + bin_edges[i + 1]) / 2)
            bin_means.append(err_arr[mask].mean())
    ax.plot(bin_centers, bin_means, "r-o", linewidth=2, markersize=5,
            label="Mean error")
    ax.set_xlabel("Piece Count [active features]")
    ax.set_ylabel("Absolute Error [cp]")
    ax.set_title("Prediction Error by Game Phase")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(plot_dir / "error_by_phase.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Win/draw/loss calibration: actual win% vs predicted score bucket
    paired = list(zip(all_predicted, all_results))
    paired.sort(key=lambda x: x[0])
    bucket_size = max(1, len(paired) // 15)
    bucket_scores = []
    bucket_winrates = []
    for i in range(0, len(paired), bucket_size):
        bucket = paired[i:i + bucket_size]
        if len(bucket) < 5:
            continue
        avg_score = sum(s for s, _ in bucket) / len(bucket)
        avg_result = sum(r for _, r in bucket) / len(bucket)
        bucket_scores.append(avg_score)
        bucket_winrates.append(avg_result * 100)
    if bucket_scores:
        fig, ax = plt.subplots(figsize=(10, 6))
        ax.plot(bucket_scores, bucket_winrates, "b-o", linewidth=2, markersize=6)
        xs = np.linspace(min(bucket_scores), max(bucket_scores), 200)
        expected = 100.0 / (1.0 + np.exp(-xs / SCORE_SCALE))
        ax.plot(xs, expected, "r--", linewidth=1, alpha=0.7, label="Expected (sigmoid)")
        ax.set_xlabel("Predicted Score [cp]")
        ax.set_ylabel("Actual Win Rate [%]")
        ax.set_title("Win Rate Calibration")
        ax.legend()
        ax.grid(True, alpha=0.3)
        fig.savefig(plot_dir / "calibration.png", dpi=150, bbox_inches="tight")
        plt.close(fig)

    print(f"  Plots saved to: {plot_dir}")


def get_run_tag():
    """Return a timestamped run tag: YYYY-MM-DD_HH-MM-SS_<githash>."""
    import subprocess
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        commit = "unknown"
    return f"{ts}_{commit}"


def resolve_latest_data(base_dir):
    """Find training data under base_dir.

    Priority: combined/ > latest timestamped subdir > base_dir itself.
    Always returns .txt so the caller's staleness check can re-preprocess
    the .bin if the .txt has been updated.
    """
    base = Path(base_dir)
    if not base.is_dir():
        return base_dir

    def find_data(directory):
        txt_path = directory / "training_data.txt"
        if txt_path.exists():
            return str(txt_path)
        bin_path = directory / "training_data.bin"
        if bin_path.exists():
            return str(bin_path)
        return None

    combined = find_data(base / "combined")
    if combined:
        return combined

    subdirs = sorted(
        [d for d in base.iterdir() if d.is_dir() and d.name != "combined"],
        key=lambda d: d.name,
        reverse=True,
    )
    if subdirs:
        result = find_data(subdirs[0])
        if result:
            return result
    fallback = find_data(base)
    if fallback:
        return fallback
    return base_dir


def train(args):
    if args.device:
        device = torch.device(args.device)
    elif torch.cuda.is_available():
        device = torch.device("cuda")
    elif torch.backends.mps.is_available():
        device = torch.device("mps")
    else:
        device = torch.device("cpu")
    print(f"Device: {device}")

    if device.type == "cpu":
        # On CPU the backward + optimizer step dominate (~90% of batch time), so
        # give the main process every core for intra-op parallelism. Workers get
        # pinned to a single thread each (see _worker_init) to avoid oversubscription.
        torch.set_num_threads(os.cpu_count() or 1)
        print(f"Torch intra-op threads: {torch.get_num_threads()}")

    data_path = args.data
    if Path(data_path).is_dir():
        data_path = resolve_latest_data(data_path)
        print(f"Resolved data: {data_path}")

    data_path = ensure_binary(data_path, label="training")

    val_data_path = args.val_data
    if val_data_path is None:
        candidate = Path(data_path).parent / "validation_data.txt"
        if candidate.exists() and candidate.stat().st_size > 0:
            val_data_path = str(candidate)
            print(f"Auto-detected validation data: {val_data_path}")
        candidate_bin = Path(data_path).parent / "validation_data.bin"
        if candidate_bin.exists() and candidate_bin.stat().st_size > 0:
            val_data_path = str(candidate_bin)
            print(f"Auto-detected validation data: {val_data_path}")

    if val_data_path:
        # Convert val through the same binary path as train so both emit 7-field
        # records; a .txt val set would decode to 5-tuples and break binary_collate.
        val_data_path = ensure_binary(val_data_path, label="validation")

    dataset = load_dataset(data_path)
    if len(dataset) == 0:
        sys.exit("No training data found.")

    use_block = (isinstance(dataset, BinaryNnueDataset)
                 and args.shuffle_block > 0)
    train_indices = None   # underlying memmap positions, set only when block-shuffling
    val_indices = None

    if val_data_path:
        train_set = dataset
        val_set = load_dataset(val_data_path)
        if len(val_set) == 0:
            sys.exit("No validation data found.")
        train_size = len(train_set)
        val_size = len(val_set)
        print(f"Training: {train_size}  Validation: {val_size} (separate file, no contamination)")
    else:
        val_size = max(1, int(len(dataset) * args.val_split))
        train_size = len(dataset) - val_size
        if use_block:
            # Split by explicit indices over the single dataset so the train
            # loader can sample the underlying memmap directly — a random_split
            # Subset would hide file positions from the locality-aware sampler.
            perm = np.random.default_rng(42).permutation(len(dataset))
            val_indices = perm[:val_size]
            train_indices = perm[val_size:]
            train_set = dataset
            val_set = dataset
        else:
            train_set, val_set = torch.utils.data.random_split(
                dataset, [train_size, val_size],
                generator=torch.Generator().manual_seed(42),
            )
        print(f"Training: {train_size}  Validation: {val_size} (random split)")

    pin = (device.type == "cuda")
    collate = binary_collate if isinstance(dataset, BinaryNnueDataset) else None

    # Profiling (2026-08-25) showed data loading is only ~3% of batch time -- the
    # optimizer/backward dominate -- so a handful of workers saturates the pipeline.
    # More than that just burns RAM (8+ persistent workers OOM-rebooted a 16 GB Mac
    # against a near-RAM memmap). Keep this modest; workers < 0 means "auto".
    num_workers = args.workers if args.workers is not None and args.workers >= 0 \
        else (os.cpu_count() or 0)
    loader_kwargs = dict(
        batch_size=args.batch, pin_memory=pin, collate_fn=collate,
        num_workers=num_workers,
    )
    if num_workers > 0:
        # Keep workers alive across epochs and let each pre-stage a few batches
        # so the device never stalls waiting on the input pipeline. Pin each
        # worker to one thread so N workers don't fight the main process (and
        # each other) for the same cores.
        loader_kwargs["persistent_workers"] = True
        loader_kwargs["prefetch_factor"] = args.prefetch
        loader_kwargs["worker_init_fn"] = _worker_init

    # Validation runs a short, sequential sweep once per epoch, so it gains almost
    # nothing from a worker pool -- but a persistent one would keep 8 extra Python
    # processes (each with its own memmap views and prefetch buffers) resident
    # through every training epoch. On a near-RAM dataset that private/anonymous
    # footprint stacks on top of the training workers and drives the machine into
    # the memory compressor. Decode val batches inline in the main process instead.
    val_loader_kwargs = dict(
        batch_size=args.batch, pin_memory=pin, collate_fn=collate,
        num_workers=0,
    )
    print(f"DataLoader workers: {num_workers}"
          + (f" (prefetch_factor={args.prefetch})" if num_workers > 0 else "")
          + "; val workers: 0 (inline)")

    if use_block:
        if train_indices is None:
            # Separate val file: train_set is the full contiguous dataset.
            train_indices = np.arange(len(train_set))
        train_loader = DataLoader(
            train_set,
            sampler=BlockShuffleSampler(train_indices, args.shuffle_block),
            **loader_kwargs,
        )
        if val_indices is not None:
            # Val shares the memmap; read its slice in sorted order so the page
            # cache touches it roughly sequentially.
            val_loader = DataLoader(
                val_set, sampler=sorted(val_indices.tolist()), **val_loader_kwargs
            )
        else:
            val_loader = DataLoader(val_set, shuffle=False, **val_loader_kwargs)
        print(f"Block-shuffle: {args.shuffle_block:,} records/block "
              f"(bounds the memmap's page-cache working set)")
    else:
        train_loader = DataLoader(train_set, shuffle=True, **loader_kwargs)
        val_loader = DataLoader(val_set, shuffle=False, **val_loader_kwargs)

    model = NnueNetwork().to(device)
    total_params = sum(p.numel() for p in model.parameters())
    print(f"Parameters: {total_params:,}")

    # ft is dense now (see NnueNetwork), so a single Adam handles everything. Two
    # param groups keep the previous weight-decay semantics exactly: no wd on the
    # feature transformer (standard for NNUE FTs), wd on the dense layers. Wrapped
    # in a one-element list so the shared step/zero_grad/scheduler loops below are
    # unchanged.
    dense_params = [p for n, p in model.named_parameters() if n != "ft.weight"]
    optimizers = [
        torch.optim.Adam(
            [
                {"params": [model.ft.weight], "weight_decay": 0.0},
                {"params": dense_params, "weight_decay": args.wd},
            ],
            lr=args.lr,
        ),
    ]
    schedulers = [
        torch.optim.lr_scheduler.StepLR(
            opt, step_size=max(1, args.epochs // 4), gamma=0.5
        )
        for opt in optimizers
    ]

    def format_pos_count(n):
        if n >= 1_000_000:
            v = n / 1_000_000
            return f"{v:.1f}M" if v != int(v) else f"{int(v)}M"
        if n >= 1_000:
            v = n / 1_000
            return f"{v:.0f}k" if v >= 10 else f"{v:.1f}k"
        return str(n)

    run_tag = get_run_tag()
    pos_tag = format_pos_count(train_size)
    run_name = f"{run_tag}_{pos_tag}_pos"
    out_dir = Path(args.output) / run_name
    out_dir.mkdir(parents=True, exist_ok=True)

    best_val_loss = float("inf")
    best_epoch = 0
    epochs_since_best = 0  # for early stopping
    train_losses = []
    val_losses = []
    learning_rates = []

    history_path = out_dir / "training_history.csv"
    history_file = open(history_path, "w", newline="")
    history_writer = csv.writer(history_file)
    history_writer.writerow([
        "epoch", "train_loss", "val_loss", "learning_rate",
        "best_val_loss", "epoch_time_s",
    ])

    training_start = time.time()

    epoch_pbar = tqdm(range(1, args.epochs + 1), desc="Training", unit="epoch")
    for epoch in epoch_pbar:
        epoch_start = time.time()

        # --- Train ---
        model.train()
        train_loss_sum = 0.0
        train_count = 0

        for batch_idx, batch in enumerate(train_loader):
            if collate is binary_collate:
                w_idx, w_off, b_idx, b_off, score, result, stm = batch
                w_idx = w_idx.to(device)
                w_off = w_off.to(device)
                b_idx = b_idx.to(device)
                b_off = b_off.to(device)
                score = score.to(device)
                result = result.to(device)
                stm = stm.to(device)
                pred = model(w_idx, w_off, b_idx, b_off, stm).squeeze(1)
                batch_size = len(w_off)
            else:
                wf, bf, score, result, stm = batch
                wf = wf.to(device)
                bf = bf.to(device)
                score = score.to(device)
                result = result.to(device)
                stm = stm.to(device)
                pred = model(wf, bf, stm).squeeze(1)
                batch_size = wf.size(0)

            pred_sigmoid = torch.sigmoid(pred)
            target = compute_target(score, result, args.lmbda)

            loss = F.mse_loss(pred_sigmoid, target)

            for opt in optimizers:
                opt.zero_grad()
            loss.backward()
            for opt in optimizers:
                opt.step()

            train_loss_sum += loss.item() * batch_size
            train_count += batch_size

        for sched in schedulers:
            sched.step()
        train_loss = train_loss_sum / train_count

        # --- Validate ---
        model.eval()
        val_loss_sum = 0.0
        val_count = 0

        with torch.no_grad():
            for batch in val_loader:
                if collate is binary_collate:
                    w_idx, w_off, b_idx, b_off, score, result, stm = batch
                    w_idx = w_idx.to(device)
                    w_off = w_off.to(device)
                    b_idx = b_idx.to(device)
                    b_off = b_off.to(device)
                    score = score.to(device)
                    result = result.to(device)
                    stm = stm.to(device)
                    pred = model(w_idx, w_off, b_idx, b_off, stm).squeeze(1)
                    batch_size = len(w_off)
                else:
                    wf, bf, score, result, stm = batch
                    wf = wf.to(device)
                    bf = bf.to(device)
                    score = score.to(device)
                    result = result.to(device)
                    stm = stm.to(device)
                    pred = model(wf, bf, stm).squeeze(1)
                    batch_size = wf.size(0)

                pred_sigmoid = torch.sigmoid(pred)
                target = compute_target(score, result, args.lmbda)
                loss = F.mse_loss(pred_sigmoid, target)

                val_loss_sum += loss.item() * batch_size
                val_count += batch_size

        val_loss = val_loss_sum / val_count
        lr = optimizers[0].param_groups[0]["lr"]
        epoch_time = time.time() - epoch_start

        train_losses.append(train_loss)
        val_losses.append(val_loss)
        learning_rates.append(lr)

        improved = ""
        if val_loss < best_val_loss - args.min_delta:
            best_val_loss = val_loss
            best_epoch = epoch
            epochs_since_best = 0
            torch.save(model.state_dict(), out_dir / "best.pt")
            improved = " *"
        else:
            epochs_since_best += 1

        epoch_pbar.set_postfix_str(
            f"train={train_loss:.6f}  val={val_loss:.6f}  lr={lr:.2e}{improved}"
        )

        # best.pt (above) and final.pt (after the loop) always cover recovery.
        # Per-epoch snapshots are opt-in: at 42 MB each, saving all 200 wrote
        # ~8 GB per run and competed for disk bandwidth.
        if args.checkpoint_every > 0 and epoch % args.checkpoint_every == 0:
            torch.save(model.state_dict(), out_dir / f"epoch_{epoch}.pt")

        history_writer.writerow([
            epoch, f"{train_loss:.8f}", f"{val_loss:.8f}", f"{lr:.2e}",
            f"{best_val_loss:.8f}", f"{epoch_time:.1f}",
        ])
        history_file.flush()

        # Early stopping: bail out once val loss has stalled for `patience`
        # epochs. patience <= 0 disables it (run the full --epochs). best.pt is
        # already from best_epoch, so stopping early costs no model quality.
        if args.patience > 0 and epochs_since_best >= args.patience:
            epoch_pbar.close()
            print(f"\nEarly stopping at epoch {epoch}: no val improvement "
                  f"for {args.patience} epochs (best epoch {best_epoch}, "
                  f"val {best_val_loss:.6f}).")
            break

    history_file.close()
    total_time = time.time() - training_start

    # Save final model
    torch.save(model.state_dict(), out_dir / "final.pt")

    model.load_state_dict(torch.load(out_dir / "best.pt", weights_only=True))
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent

    export_path = out_dir / "best.bin"
    export_quantized(model, export_path)

    canonical_path = repo_root / "nnue" / "nnue.bin"
    print(f"  To use these weights: cp {export_path} {canonical_path}")

    # Save training metadata
    meta = {
        "run_name": run_name,
        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "git_commit": get_run_tag().split("_")[-1],
        "device": str(device),
        "train_positions": train_size,
        "val_positions": val_size,
        "epochs": args.epochs,
        "batch_size": args.batch,
        "learning_rate": args.lr,
        "weight_decay": args.wd,
        "lambda": args.lmbda,
        "val_split": args.val_split,
        "best_epoch": best_epoch,
        "best_val_loss": best_val_loss,
        "final_train_loss": train_losses[-1],
        "total_time_s": round(total_time, 1),
        "data_path": str(data_path),
    }
    with open(out_dir / "training_meta.json", "w") as f:
        json.dump(meta, f, indent=2)

    model.to(device)
    plot_dir = repo_root / "results" / "nnue_training" / run_name
    generate_plots(train_losses, val_losses, plot_dir,
                   model, val_loader, device, args.lmbda)

    print(f"\nTraining complete.")
    print(f"  Best validation loss: {best_val_loss:.6f} (epoch {best_epoch})")
    print(f"  Total time: {total_time / 60:.1f} min")
    print(f"  History: {history_path}")
    print(f"  PyTorch checkpoints: {out_dir}")
    print(f"  Quantized weights: {export_path}")


# --------------------------------------------------------------------------- #
#  Weight export for C++ inference
# --------------------------------------------------------------------------- #

# Quantization scheme:
#   Feature transformer weights/biases: int16 (scale = 127)
#   Hidden layer weights: int8 (scale = 64)
#   Hidden layer biases: int32 (scale = 127 * 64 = 8128)
#   Output weights: int8 (scale = 64)
#   Output bias: int32 (scale = 127 * 64 = 8128)

FT_WEIGHT_SCALE = 127
HIDDEN_WEIGHT_SCALE = 64
FT_BIAS_SCALE = FT_WEIGHT_SCALE           # 127
HIDDEN_BIAS_SCALE = FT_WEIGHT_SCALE * HIDDEN_WEIGHT_SCALE  # 8128
OUTPUT_WEIGHT_SCALE = HIDDEN_WEIGHT_SCALE  # 64
OUTPUT_BIAS_SCALE = FT_WEIGHT_SCALE * OUTPUT_WEIGHT_SCALE  # 8128


def quantize_i16(tensor, scale):
    return torch.clamp(torch.round(tensor * scale), -32768, 32767).to(torch.int16)


def quantize_i8(tensor, scale):
    return torch.clamp(torch.round(tensor * scale), -128, 127).to(torch.int8)


def quantize_i32(tensor, scale):
    return torch.clamp(
        torch.round(tensor * scale), -(2**31), 2**31 - 1
    ).to(torch.int32)


def export_quantized(model, path):
    """Export model weights in a flat binary format for C++ inference.

    Layout:
        Header: 4 bytes magic "OZNN"
        4 x int32: HALFKP_SIZE, L1_SIZE, L2_SIZE, L3_SIZE
        Feature transformer weight: int16[L1_SIZE][HALFKP_SIZE]
        Feature transformer bias:   int16[L1_SIZE]
        L2 weight: int8[L2_SIZE][L1_SIZE * 2]
        L2 bias:   int32[L2_SIZE]
        L3 weight: int8[L3_SIZE][L2_SIZE]
        L3 bias:   int32[L3_SIZE]
        Output weight: int8[1][L3_SIZE]
        Output bias:   int32[1]
    """
    model.eval()
    model.cpu()
    sd = model.state_dict()

    ft_w = quantize_i16(sd["ft.weight"].t(), FT_WEIGHT_SCALE)
    ft_b = quantize_i16(sd["ft_bias"], FT_BIAS_SCALE)
    l2_w = quantize_i8(sd["l2.weight"], HIDDEN_WEIGHT_SCALE)
    l2_b = quantize_i32(sd["l2.bias"], HIDDEN_BIAS_SCALE)
    l3_w = quantize_i8(sd["l3.weight"], HIDDEN_WEIGHT_SCALE)
    l3_b = quantize_i32(sd["l3.bias"], HIDDEN_BIAS_SCALE)
    out_w = quantize_i8(sd["output.weight"], OUTPUT_WEIGHT_SCALE)
    out_b = quantize_i32(sd["output.bias"], OUTPUT_BIAS_SCALE)

    with open(path, "wb") as f:
        f.write(b"OZNN")
        f.write(struct.pack("<4i", HALFKP_SIZE, L1_SIZE, L2_SIZE, L3_SIZE))

        f.write(ft_w.numpy().tobytes())
        f.write(ft_b.numpy().tobytes())
        f.write(l2_w.numpy().tobytes())
        f.write(l2_b.numpy().tobytes())
        f.write(l3_w.numpy().tobytes())
        f.write(l3_b.numpy().tobytes())
        f.write(out_w.numpy().tobytes())
        f.write(out_b.numpy().tobytes())

    file_size = os.path.getsize(path)
    print(f"Exported quantized weights to {path} ({file_size:,} bytes)")


# --------------------------------------------------------------------------- #
#  Entry point
# --------------------------------------------------------------------------- #

def replot(args):
    """Regenerate all plots from a previous training run."""
    run_dir = Path(args.run)
    if not run_dir.is_dir():
        sys.exit(f"Run directory not found: {run_dir}")

    history_path = run_dir / "training_history.csv"
    if not history_path.exists():
        sys.exit(f"No training_history.csv in {run_dir}")

    checkpoint_path = run_dir / "best.pt"
    if not checkpoint_path.exists():
        sys.exit(f"No best.pt in {run_dir}")

    train_losses = []
    val_losses = []
    with open(history_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            train_losses.append(float(row["train_loss"]))
            val_losses.append(float(row["val_loss"]))

    if not train_losses:
        sys.exit("training_history.csv is empty")

    print(f"Loaded {len(train_losses)} epochs from {history_path}")

    val_data_path = args.val_data
    if val_data_path is None:
        meta_path = run_dir / "training_meta.json"
        if meta_path.exists():
            with open(meta_path) as f:
                meta = json.load(f)
            data_path = meta.get("data_path", "")
            candidate = Path(data_path).parent / "validation_data.bin"
            if candidate.exists() and candidate.stat().st_size > 0:
                val_data_path = str(candidate)
            else:
                candidate = Path(data_path).parent / "validation_data.txt"
                if candidate.exists() and candidate.stat().st_size > 0:
                    val_data_path = str(candidate)

    if args.device:
        device = torch.device(args.device)
    elif torch.cuda.is_available():
        device = torch.device("cuda")
    elif torch.backends.mps.is_available():
        device = torch.device("mps")
    else:
        device = torch.device("cpu")
    print(f"Device: {device}")

    model = NnueNetwork().to(device)
    model.load_state_dict(torch.load(checkpoint_path, map_location=device, weights_only=True))
    print(f"Loaded checkpoint: {checkpoint_path}")

    val_loader = None
    if val_data_path:
        val_set = load_dataset(val_data_path)
        if len(val_set) > 0:
            collate = binary_collate if isinstance(val_set, BinaryNnueDataset) else None
            val_loader = DataLoader(
                val_set, batch_size=args.batch, shuffle=False,
                num_workers=0, collate_fn=collate,
            )
            print(f"Validation data: {val_data_path} ({len(val_set):,} positions)")
        else:
            print(f"Warning: validation data empty, skipping score accuracy plot")
    else:
        print("No validation data found, skipping score accuracy plot")

    meta_path = run_dir / "training_meta.json"
    lmbda = 0.7
    if meta_path.exists():
        with open(meta_path) as f:
            meta = json.load(f)
        lmbda = meta.get("lambda", 0.7)

    repo_root = Path(__file__).resolve().parent.parent
    plot_dir = repo_root / "results" / "nnue_training" / run_dir.name
    generate_plots(train_losses, val_losses, plot_dir,
                   model, val_loader, device, lmbda)

    print(f"\nPlots regenerated in: {plot_dir}")


def load_config():
    cfg_path = os.path.join(os.path.dirname(__file__), "..", "nnue", "config.json")
    try:
        with open(cfg_path) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def main():
    cfg = load_config()
    t = cfg.get("training", {})
    repo_root = str(Path(__file__).resolve().parent.parent)

    def resolve_default(key, fallback):
        val = t.get(key, fallback)
        if val and not os.path.isabs(val):
            return os.path.join(repo_root, val)
        return val

    parser = argparse.ArgumentParser(
        description="Train an NNUE evaluation network for OmegaZero"
    )
    subparsers = parser.add_subparsers(dest="command")

    # train subcommand
    train_parser = subparsers.add_parser("train", help="Train a new NNUE model")
    train_parser.add_argument(
        "--data", default=resolve_default("data", "nnue/data"),
        help="Path to training data file or directory (auto-resolves latest run)",
    )
    train_parser.add_argument(
        "--val-data", default=resolve_default("val_data", None),
        help="Path to separate validation data file (from different games, avoids contamination)",
    )
    train_parser.add_argument(
        "--output", default=resolve_default("output", "nnue/model"),
        help="Output directory for model checkpoints (default: nnue/model)",
    )
    train_parser.add_argument(
        "--epochs", type=int, default=t.get("epochs", 200),
        help="Number of training epochs (default: 200)",
    )
    train_parser.add_argument(
        "--batch", type=int, default=t.get("batch", 16384),
        help="Batch size (default: 16384)",
    )
    train_parser.add_argument(
        "--lr", type=float, default=t.get("lr", 1e-3),
        help="Initial learning rate (default: 0.001)",
    )
    train_parser.add_argument(
        "--wd", type=float, default=t.get("wd", 1e-6),
        help="Weight decay (default: 1e-6)",
    )
    train_parser.add_argument(
        "--lmbda", type=float, default=t.get("lmbda", 0.7),
        help="Blend factor: lambda*sigmoid(score) + (1-lambda)*result (default: 0.7)",
    )
    train_parser.add_argument(
        "--val-split", type=float, default=t.get("val_split", 0.05),
        help="Fraction of data for validation (default: 0.05)",
    )
    train_parser.add_argument(
        "--device", default=t.get("device"),
        help="Device: cpu, mps, or cuda (default: auto-detect)",
    )
    train_parser.add_argument(
        "--workers", type=int, default=t.get("workers", -1),
        help="DataLoader worker processes; -1 = auto (all logical cores) (default: -1)",
    )
    train_parser.add_argument(
        "--prefetch", type=int, default=t.get("prefetch", 4),
        help="Batches each worker prefetches ahead (default: 4)",
    )
    train_parser.add_argument(
        "--shuffle-block", type=int, default=t.get("shuffle_block", 8_000_000),
        help="Locality-preserving block shuffle for memmap (.bin) data: shuffle "
             "within contiguous blocks of N records instead of globally, bounding "
             "the page-cache working set to ~one block (~134 B/record). Defaults on "
             "to keep near-RAM datasets from causing the system-wide memory pressure "
             "that can hang the machine; harmless on small data (a dataset that fits "
             "in one block is shuffled globally). 0 = force global shuffle "
             "(default: 8000000, ~1 GB working set)",
    )
    train_parser.add_argument(
        "--checkpoint-every", type=int, default=t.get("checkpoint_every", 0),
        help="Save a per-epoch checkpoint every N epochs; 0 = off "
             "(best.pt/final.pt are always saved) (default: 0)",
    )
    train_parser.add_argument(
        "--patience", type=int, default=t.get("patience", 0),
        help="Early stop after N epochs with no val improvement; 0 = off "
             "(run full --epochs) (default: 0)",
    )
    train_parser.add_argument(
        "--min-delta", type=float, default=t.get("min_delta", 0.0),
        help="Minimum val-loss decrease to count as an improvement for "
             "early stopping (default: 0.0)",
    )

    # plot subcommand
    plot_parser = subparsers.add_parser("plot", help="Regenerate plots from a previous run")
    plot_parser.add_argument(
        "--run", required=True,
        help="Path to training run directory (contains training_history.csv and best.pt)",
    )
    plot_parser.add_argument(
        "--val-data", default=None,
        help="Path to validation data file (default: auto-detect from training_meta.json)",
    )
    plot_parser.add_argument(
        "--batch", type=int, default=4096,
        help="Batch size for inference (default: 4096)",
    )
    plot_parser.add_argument(
        "--device", default=None,
        help="Device: cpu, mps, or cuda (default: auto-detect)",
    )

    args = parser.parse_args()

    if args.command == "plot":
        replot(args)
    else:
        if args.command is None:
            args = train_parser.parse_args()
        train(args)


if __name__ == "__main__":
    main()
