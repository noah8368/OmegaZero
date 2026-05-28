#!/usr/bin/env python3
"""
Preprocess NNUE training data from text to binary format for memory-efficient training.

Converts the text format (FEN | score | result) to a compact binary format that
can be memory-mapped during training, avoiding OOM at large dataset sizes.

Usage:
    python3 scripts/preprocess_data.py nnue/data/combined/training_data.txt
    python3 scripts/preprocess_data.py nnue/data/combined/training_data.txt -o custom_output.bin
"""

import argparse
import os
import sys
import numpy as np
from pathlib import Path
from tqdm import tqdm


NUM_KING_SQUARES = 64
NUM_PIECE_TYPES = 10
NUM_PIECE_SQUARES = 64
MAX_FEATURES = 32

WHITE_PIECE_INDEX = {"P": 0, "N": 1, "B": 2, "R": 3, "Q": 4}
BLACK_PIECE_INDEX = {"p": 5, "n": 6, "b": 7, "r": 8, "q": 9}

RECORD_DTYPE = np.dtype([
    ("num_white", np.uint8),
    ("white_indices", np.uint16, (MAX_FEATURES,)),
    ("num_black", np.uint8),
    ("black_indices", np.uint16, (MAX_FEATURES,)),
    ("score", np.int16),
    ("result", np.uint8),   # 0=loss(0.0), 1=draw(0.5), 2=win(1.0)
    ("stm", np.uint8),      # 0=black, 1=white
])


def mirror_sq(sq):
    return sq ^ 56


def halfkp_index(king_sq, piece_index, piece_sq):
    return king_sq * (NUM_PIECE_TYPES * NUM_PIECE_SQUARES) + \
           piece_index * NUM_PIECE_SQUARES + piece_sq


def fen_to_halfkp(fen):
    parts = fen.split()
    board_str = parts[0]

    pieces = []
    white_king_sq = None
    black_king_sq = None

    sq = 56
    for ch in board_str:
        if ch == "/":
            sq -= 16
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
        if piece_char.isupper():
            pi_white = WHITE_PIECE_INDEX.get(piece_char)
        else:
            pi_white = BLACK_PIECE_INDEX.get(piece_char)
        if pi_white is not None:
            white_features.append(halfkp_index(white_king_sq, pi_white, piece_sq))

        mirrored_piece_sq = mirror_sq(piece_sq)
        mirrored_king_sq = mirror_sq(black_king_sq)
        if piece_char.isupper():
            pi_black = BLACK_PIECE_INDEX.get(piece_char.lower())
        else:
            pi_black = WHITE_PIECE_INDEX.get(piece_char.upper())
        if pi_black is not None:
            black_features.append(halfkp_index(mirrored_king_sq, pi_black, mirrored_piece_sq))

    return white_features, black_features


def main():
    parser = argparse.ArgumentParser(
        description="Preprocess NNUE training data to binary format"
    )
    parser.add_argument("input", help="Input text file (FEN | score | result)")
    parser.add_argument(
        "-o", "--output",
        help="Output binary file (default: same path with .bin extension)",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    if args.output:
        output_path = Path(args.output)
    else:
        output_path = input_path.with_suffix(".bin")

    print(f"Counting positions in {input_path}...")
    num_lines = 0
    with open(input_path) as f:
        for line in f:
            if line.strip():
                num_lines += 1
    print(f"  {num_lines:,} positions")

    data = np.memmap(output_path, dtype=RECORD_DTYPE, mode="w+", shape=(num_lines,))

    print("Converting to binary format...")
    idx = 0
    skipped = 0
    with open(input_path) as f:
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
        with open(output_path, "r+b") as f:
            f.truncate(idx * RECORD_DTYPE.itemsize)

    file_size = os.path.getsize(output_path)
    print(f"\nDone:")
    print(f"  Positions: {idx:,} ({skipped:,} skipped)")
    print(f"  Output: {output_path} ({file_size / 1024 / 1024:.1f} MB)")
    print(f"  Record size: {RECORD_DTYPE.itemsize} bytes")


if __name__ == "__main__":
    main()
