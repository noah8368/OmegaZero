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
    python3 scripts/train_nnue.py --data scripts/nnue_training_data/training_data.txt
    python3 scripts/train_nnue.py --data scripts/nnue_training_data/training_data.txt --epochs 200 --batch 16384
"""

import argparse
import math
import os
import struct
import sys
import time
from pathlib import Path

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
                              +--> feature_transform (shared) --> ClippedReLU
        black_input (40960) --+

        Concat(white_accum, black_accum) based on side to move
        --> 512 --> Linear(32) --> ClippedReLU
                --> Linear(32) --> ClippedReLU
                --> Linear(1)
    """

    def __init__(self):
        super().__init__()
        self.feature_transform = nn.Linear(HALFKP_SIZE, L1_SIZE)
        self.l2 = nn.Linear(L1_SIZE * 2, L2_SIZE)
        self.l3 = nn.Linear(L2_SIZE, L3_SIZE)
        self.output = nn.Linear(L3_SIZE, 1)
        self.clipped_relu = ClippedReLU()

        self._init_weights()

    def _init_weights(self):
        nn.init.kaiming_normal_(self.feature_transform.weight, nonlinearity="relu")
        nn.init.zeros_(self.feature_transform.bias)
        nn.init.kaiming_normal_(self.l2.weight, nonlinearity="relu")
        nn.init.zeros_(self.l2.bias)
        nn.init.kaiming_normal_(self.l3.weight, nonlinearity="relu")
        nn.init.zeros_(self.l3.bias)
        nn.init.xavier_normal_(self.output.weight)
        nn.init.zeros_(self.output.bias)

    def forward(self, white_features, black_features, stm):
        """Forward pass.

        Args:
            white_features: (batch, 40960) sparse binary input
            black_features: (batch, 40960) sparse binary input
            stm: (batch,) side to move, 1.0 = white, 0.0 = black

        Returns:
            (batch, 1) raw evaluation output (pre-sigmoid)
        """
        white_accum = self.clipped_relu(self.feature_transform(white_features))
        black_accum = self.clipped_relu(self.feature_transform(black_features))

        # Concatenate with side-to-move's perspective first.
        # When white to move: [white_accum, black_accum]
        # When black to move: [black_accum, white_accum]
        stm_expanded = stm.unsqueeze(1)  # (batch, 1)
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


def train(args):
    device = torch.device("mps" if torch.backends.mps.is_available()
                          else "cuda" if torch.cuda.is_available()
                          else "cpu")
    print(f"Device: {device}")

    dataset = NnueDataset(args.data)
    if len(dataset) == 0:
        sys.exit("No training data found.")

    val_size = max(1, int(len(dataset) * args.val_split))
    train_size = len(dataset) - val_size
    train_set, val_set = torch.utils.data.random_split(
        dataset, [train_size, val_size],
        generator=torch.Generator().manual_seed(42),
    )

    print(f"Training: {train_size}  Validation: {val_size}")

    train_loader = DataLoader(
        train_set, batch_size=args.batch, shuffle=True,
        num_workers=0, pin_memory=(device.type != "cpu"),
    )
    val_loader = DataLoader(
        val_set, batch_size=args.batch, shuffle=False,
        num_workers=0, pin_memory=(device.type != "cpu"),
    )

    model = NnueNetwork().to(device)
    total_params = sum(p.numel() for p in model.parameters())
    print(f"Parameters: {total_params:,}")

    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.wd)
    scheduler = torch.optim.lr_scheduler.StepLR(
        optimizer, step_size=max(1, args.epochs // 4), gamma=0.5
    )

    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    best_val_loss = float("inf")

    epoch_pbar = tqdm(range(1, args.epochs + 1), desc="Training", unit="epoch")
    for epoch in epoch_pbar:
        # --- Train ---
        model.train()
        train_loss_sum = 0.0
        train_count = 0

        for wf, bf, score, result, stm in train_loader:
            wf = wf.to(device)
            bf = bf.to(device)
            score = score.to(device)
            result = result.to(device)
            stm = stm.to(device)

            pred = model(wf, bf, stm).squeeze(1)
            pred_sigmoid = torch.sigmoid(pred)
            target = compute_target(score, result, args.lmbda)

            loss = F.mse_loss(pred_sigmoid, target)

            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

            train_loss_sum += loss.item() * wf.size(0)
            train_count += wf.size(0)

        scheduler.step()
        train_loss = train_loss_sum / train_count

        # --- Validate ---
        model.eval()
        val_loss_sum = 0.0
        val_count = 0

        with torch.no_grad():
            for wf, bf, score, result, stm in val_loader:
                wf = wf.to(device)
                bf = bf.to(device)
                score = score.to(device)
                result = result.to(device)
                stm = stm.to(device)

                pred = model(wf, bf, stm).squeeze(1)
                pred_sigmoid = torch.sigmoid(pred)
                target = compute_target(score, result, args.lmbda)
                loss = F.mse_loss(pred_sigmoid, target)

                val_loss_sum += loss.item() * wf.size(0)
                val_count += wf.size(0)

        val_loss = val_loss_sum / val_count
        lr = optimizer.param_groups[0]["lr"]

        improved = ""
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            torch.save(model.state_dict(), out_dir / "best.pt")
            improved = " *"

        epoch_pbar.set_postfix_str(
            f"train={train_loss:.6f}  val={val_loss:.6f}  lr={lr:.2e}{improved}"
        )

        if epoch % args.save_every == 0:
            torch.save(model.state_dict(), out_dir / f"epoch_{epoch}.pt")

    # Save final model
    torch.save(model.state_dict(), out_dir / "final.pt")

    # Export quantized weights to repo root (next to p3ECO.txt)
    model.load_state_dict(torch.load(out_dir / "best.pt", weights_only=True))
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent
    export_path = repo_root / "nnue.bin"
    export_quantized(model, export_path)
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
#   Output bias: int32 (scale = 64 * 64 = 4096)

FT_WEIGHT_SCALE = 127
HIDDEN_WEIGHT_SCALE = 64
FT_BIAS_SCALE = FT_WEIGHT_SCALE           # 127
HIDDEN_BIAS_SCALE = FT_WEIGHT_SCALE * HIDDEN_WEIGHT_SCALE  # 8128
OUTPUT_WEIGHT_SCALE = HIDDEN_WEIGHT_SCALE  # 64
OUTPUT_BIAS_SCALE = HIDDEN_WEIGHT_SCALE * OUTPUT_WEIGHT_SCALE  # 4096


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

    ft_w = quantize_i16(sd["feature_transform.weight"], FT_WEIGHT_SCALE)
    ft_b = quantize_i16(sd["feature_transform.bias"], FT_BIAS_SCALE)
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

def main():
    parser = argparse.ArgumentParser(
        description="Train an NNUE evaluation network for OmegaZero"
    )
    parser.add_argument(
        "--data", default="scripts/nnue_training_data/training_data.txt",
        help="Path to training data file",
    )
    parser.add_argument(
        "--output", default="scripts/nnue_training_data/model",
        help="Output directory for model checkpoints (default: scripts/nnue_training_data/model)",
    )
    parser.add_argument(
        "--epochs", type=int, default=200,
        help="Number of training epochs (default: 200)",
    )
    parser.add_argument(
        "--batch", type=int, default=16384,
        help="Batch size (default: 16384)",
    )
    parser.add_argument(
        "--lr", type=float, default=1e-3,
        help="Initial learning rate (default: 0.001)",
    )
    parser.add_argument(
        "--wd", type=float, default=1e-6,
        help="Weight decay (default: 1e-6)",
    )
    parser.add_argument(
        "--lmbda", type=float, default=0.7,
        help="Blend factor: lambda*sigmoid(score) + (1-lambda)*result (default: 0.7)",
    )
    parser.add_argument(
        "--val-split", type=float, default=0.05,
        help="Fraction of data for validation (default: 0.05)",
    )
    parser.add_argument(
        "--save-every", type=int, default=50,
        help="Save checkpoint every N epochs (default: 50)",
    )
    args = parser.parse_args()

    train(args)


if __name__ == "__main__":
    main()
