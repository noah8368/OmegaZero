#!/usr/bin/env python3
"""
Train an NNUE evaluation network for OmegaZero.

Architecture: HalfKP (Half King-Piece)
  - Input: 40960 sparse features per perspective
           (64 king_sq x 10 piece_types x 64 piece_sq)
  - Feature transformer: 40960 -> 256 (shared weights), clipped ReLU
  - Concat white + black perspectives -> 512
  - 512 -> 32 -> 32 -> 1

Training target: blend of sigmoid(search_score) and game outcome.

Usage:
    python3 scripts/train_nnue.py --data nnue/data/<run>/training_data.txt
    python3 scripts/train_nnue.py --data nnue/data/<run>/training_data.txt --val-data nnue/data/<run>/validation_data.txt
"""

import argparse
import os
import shutil
import struct
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader
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
        file_size = os.path.getsize(path)
        self.num_records = file_size // RECORD_DTYPE.itemsize
        self.data = np.memmap(
            path, dtype=RECORD_DTYPE, mode="r", shape=(self.num_records,)
        )
        print(f"Loaded {self.num_records:,} positions from {path} (memory-mapped)")

    def __len__(self):
        return self.num_records

    def __getitem__(self, idx):
        record = self.data[idx]
        nw = int(record["num_white"])
        nb = int(record["num_black"])
        return (record["white_indices"][:nw].copy(),
                record["black_indices"][:nb].copy(),
                float(record["score"]),
                float(record["result"]) / 2.0,
                float(record["stm"]))


def binary_collate(batch):
    """Pack sparse indices for EmbeddingBag — no dense tensors needed."""
    white_indices = []
    black_indices = []
    white_offsets = [0]
    black_offsets = [0]
    scores = []
    results = []
    stms = []

    for wf, bf, score, result, stm in batch:
        white_indices.append(torch.from_numpy(wf.astype(np.int64)))
        black_indices.append(torch.from_numpy(bf.astype(np.int64)))
        white_offsets.append(white_offsets[-1] + len(wf))
        black_offsets.append(black_offsets[-1] + len(bf))
        scores.append(score)
        results.append(result)
        stms.append(stm)

    return (torch.cat(white_indices),
            torch.tensor(white_offsets[:-1], dtype=torch.long),
            torch.cat(black_indices),
            torch.tensor(black_offsets[:-1], dtype=torch.long),
            torch.tensor(scores, dtype=torch.float32),
            torch.tensor(results, dtype=torch.float32),
            torch.tensor(stms, dtype=torch.float32))


def load_dataset(path):
    """Load a dataset from either .bin (binary) or .txt (text) format."""
    if path.endswith(".bin"):
        return BinaryNnueDataset(path)
    return NnueDataset(path)


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
        self.ft = nn.EmbeddingBag(HALFKP_SIZE, L1_SIZE, mode="sum")
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

    # Loss curve
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(epochs, train_losses, label="Train Loss")
    ax.plot(epochs, val_losses, label="Val Loss")
    best_epoch = val_losses.index(min(val_losses)) + 1
    ax.axvline(best_epoch, color="gray", linestyle="--", alpha=0.5,
               label=f"Best val (epoch {best_epoch})")
    ax.set_xlabel("Epoch")
    ax.set_ylabel("MSE Loss")
    ax.set_title("NNUE Training Loss")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(plot_dir / "loss.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Collect predictions on validation set
    model.eval()
    all_predicted = []
    all_actual = []
    all_results = []
    all_material = []
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

    # Score accuracy: predicted vs actual
    fig, ax = plt.subplots(figsize=(8, 8))
    ax.scatter(all_actual, all_predicted, alpha=0.15, s=4)
    lo = min(min(all_actual), min(all_predicted))
    hi = max(max(all_actual), max(all_predicted))
    ax.plot([lo, hi], [lo, hi], "r--", linewidth=1, label="Perfect prediction")
    ax.set_xlabel("Actual Score (cp)")
    ax.set_ylabel("Predicted Score (cp)")
    ax.set_title("NNUE Score Accuracy (Validation Set)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_aspect("equal")
    fig.savefig(plot_dir / "score_accuracy.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Score distribution: predicted vs actual histograms
    fig, ax = plt.subplots(figsize=(10, 6))
    clip = 2000
    clipped_actual = [max(-clip, min(clip, s)) for s in all_actual]
    clipped_predicted = [max(-clip, min(clip, s)) for s in all_predicted]
    bins = 80
    ax.hist(clipped_actual, bins=bins, alpha=0.5, label="Actual", density=True)
    ax.hist(clipped_predicted, bins=bins, alpha=0.5, label="Predicted", density=True)
    ax.set_xlabel("Score (cp)")
    ax.set_ylabel("Density")
    ax.set_title("Score Distribution (Validation Set)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(plot_dir / "score_distribution.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # Error by game phase: prediction error vs piece count
    errors = [abs(p - a) for p, a in zip(all_predicted, all_actual)]
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.scatter(all_material, errors, alpha=0.1, s=4)
    # Bin by material count and plot mean error
    import numpy as np
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
    ax.set_xlabel("Piece Count (active features)")
    ax.set_ylabel("Absolute Error (cp)")
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
        ax.set_xlabel("Predicted Score (cp)")
        ax.set_ylabel("Actual Win Rate (%)")
        ax.set_title("Win Rate Calibration")
        ax.legend()
        ax.grid(True, alpha=0.3)
        fig.savefig(plot_dir / "calibration.png", dpi=150, bbox_inches="tight")
        plt.close(fig)

    print(f"  Plots saved to: {plot_dir}")


def get_run_tag():
    """Return a timestamped run tag: YYYY-MM-DD_HH-MM-SS_<githash>."""
    import subprocess
    from datetime import datetime
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
    else:
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")

    data_path = args.data
    if Path(data_path).is_dir():
        data_path = resolve_latest_data(data_path)
        print(f"Resolved data: {data_path}")

    if data_path.endswith(".bin"):
        txt_path = data_path[:-4] + ".txt"
        if Path(txt_path).exists() and \
                Path(txt_path).stat().st_mtime > Path(data_path).stat().st_mtime:
            print(f"Source .txt is newer than .bin — re-preprocessing...")
            preprocess_to_binary(txt_path, data_path)
        else:
            print(f"Using binary: {data_path}")
    elif data_path.endswith(".txt"):
        bin_path = data_path[:-4] + ".bin"
        if not Path(bin_path).exists() or \
                Path(data_path).stat().st_mtime > Path(bin_path).stat().st_mtime:
            print("Preprocessing text data to binary format...")
            preprocess_to_binary(data_path, bin_path)
        else:
            print(f"Using cached binary: {bin_path}")
        data_path = bin_path

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

    dataset = load_dataset(data_path)
    if len(dataset) == 0:
        sys.exit("No training data found.")

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
        train_set, val_set = torch.utils.data.random_split(
            dataset, [train_size, val_size],
            generator=torch.Generator().manual_seed(42),
        )
        print(f"Training: {train_size}  Validation: {val_size} (random split)")

    pin = (device.type == "cuda")
    collate = binary_collate if isinstance(dataset, BinaryNnueDataset) else None
    train_loader = DataLoader(
        train_set, batch_size=args.batch, shuffle=True,
        num_workers=0, pin_memory=pin, collate_fn=collate,
    )
    val_loader = DataLoader(
        val_set, batch_size=args.batch, shuffle=False,
        num_workers=0, pin_memory=pin, collate_fn=collate,
    )

    model = NnueNetwork().to(device)
    total_params = sum(p.numel() for p in model.parameters())
    print(f"Parameters: {total_params:,}")

    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.wd)
    scheduler = torch.optim.lr_scheduler.StepLR(
        optimizer, step_size=max(1, args.epochs // 4), gamma=0.5
    )

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
    train_losses = []
    val_losses = []
    learning_rates = []

    epoch_pbar = tqdm(range(1, args.epochs + 1), desc="Training", unit="epoch")
    for epoch in epoch_pbar:
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

            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            train_loss_sum += loss.item() * batch_size
            train_count += batch_size

        scheduler.step()
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
        lr = optimizer.param_groups[0]["lr"]

        train_losses.append(train_loss)
        val_losses.append(val_loss)
        learning_rates.append(lr)

        improved = ""
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            torch.save(model.state_dict(), out_dir / "best.pt")
            improved = " *"

        epoch_pbar.set_postfix_str(
            f"train={train_loss:.6f}  val={val_loss:.6f}  lr={lr:.2e}{improved}"
        )

        torch.save(model.state_dict(), out_dir / f"epoch_{epoch}.pt")

    # Save final model
    torch.save(model.state_dict(), out_dir / "final.pt")

    model.load_state_dict(torch.load(out_dir / "best.pt", weights_only=True))
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent

    export_path = out_dir / "best.bin"
    export_quantized(model, export_path)

    canonical_path = repo_root / "nnue" / "nnue.bin"
    print(f"  To use these weights: cp {export_path} {canonical_path}")

    model.to(device)
    plot_dir = repo_root / "results" / "nnue_training" / run_name
    generate_plots(train_losses, val_losses, plot_dir,
                   model, val_loader, device, args.lmbda)

    print(f"\nTraining complete.")
    print(f"  Best validation loss: {best_val_loss:.6f}")
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

def load_config():
    import json
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
    parser.add_argument(
        "--data", default=resolve_default("data", "nnue/data"),
        help="Path to training data file or directory (auto-resolves latest run)",
    )
    parser.add_argument(
        "--val-data", default=resolve_default("val_data", None),
        help="Path to separate validation data file (from different games, avoids contamination)",
    )
    parser.add_argument(
        "--output", default=resolve_default("output", "nnue/model"),
        help="Output directory for model checkpoints (default: nnue/model)",
    )
    parser.add_argument(
        "--epochs", type=int, default=t.get("epochs", 200),
        help="Number of training epochs (default: 200)",
    )
    parser.add_argument(
        "--batch", type=int, default=t.get("batch", 16384),
        help="Batch size (default: 16384)",
    )
    parser.add_argument(
        "--lr", type=float, default=t.get("lr", 1e-3),
        help="Initial learning rate (default: 0.001)",
    )
    parser.add_argument(
        "--wd", type=float, default=t.get("wd", 1e-6),
        help="Weight decay (default: 1e-6)",
    )
    parser.add_argument(
        "--lmbda", type=float, default=t.get("lmbda", 0.7),
        help="Blend factor: lambda*sigmoid(score) + (1-lambda)*result (default: 0.7)",
    )
    parser.add_argument(
        "--val-split", type=float, default=t.get("val_split", 0.05),
        help="Fraction of data for validation (default: 0.05)",
    )
    parser.add_argument(
        "--device", default=t.get("device"),
        help="Device: cpu, mps, or cuda (default: auto-detect)",
    )
    args = parser.parse_args()

    train(args)


if __name__ == "__main__":
    main()
