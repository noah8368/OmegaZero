#!/usr/bin/env python3
"""
Preprocess NF-002 uncertainty-label data from text to binary format.

The uncertainty datagen mode (`mode: uncertainty` in nnue/config.json, see
src/datagen.cc::PlayGameUncertainty) emits a 7-field row per sampled position:

    FEN | v_hat | v_star | u | depth | nodes | result

where (all scores are STM POV, centipawns):
    v_hat  = raw static eval (Board::Evaluate(), uncorrected)
    v_star = fixed-depth + node-capped deep search score (the target)
    u      = v_hat - v_star, the signed eval error we model  p(u | x)
    depth  = deepest completed depth of the v_star search
    nodes  = nodes visited by the v_star search
    result = game outcome, White POV (0.0 / 0.5 / 1.0)

This is the analogue of scripts/preprocess_data.py for the research pipeline. It
reuses that script's HalfKP feature extraction so the conditioning input matches
the NNUE trunk exactly: the model's feature x is the frozen trunk's output, which
is computed from these same HalfKP indices. The MDN head (per H2) then reads x and
regresses the conditional distribution of u.

Usage:
    python3 scripts/preprocess_uncertainty.py nnue/data/<run>/training_data.txt
    python3 scripts/preprocess_uncertainty.py <input.txt> -o custom_output.bin
"""

import argparse
import os
import sys
import numpy as np
from pathlib import Path
from tqdm import tqdm

# Reuse the HalfKP feature extraction from the NNUE preprocessor so the
# conditioning features are byte-for-byte identical to the training pipeline.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from preprocess_data import fen_to_halfkp, MAX_FEATURES  # noqa: E402

# v_hat / v_star / u are int32, NOT int16: NF-002's signal is the fat signed-error
# tail from the KEPT tactical positions (the filters are inverted vs. NNUE datagen),
# and clipping those to +/-32767 would corrupt exactly the tail the margins read.
UNC_RECORD_DTYPE = np.dtype([
    ("num_white", np.uint8),
    ("white_indices", np.uint16, (MAX_FEATURES,)),
    ("num_black", np.uint8),
    ("black_indices", np.uint16, (MAX_FEATURES,)),
    ("stm", np.uint8),       # 0=black, 1=white
    ("v_hat", np.int32),     # raw static eval, STM POV (centipawns)
    ("v_star", np.int32),    # fixed-depth search target, STM POV (centipawns)
    ("u", np.int32),         # signed error v_hat - v_star (the modeled quantity)
    ("depth", np.uint8),     # deepest completed depth of the v_star search
    ("nodes", np.uint32),    # nodes visited by the v_star search
    ("result", np.uint8),    # 0=loss(0.0), 1=draw(0.5), 2=win(1.0), White POV
])

NUM_FIELDS = 7


def main():
    parser = argparse.ArgumentParser(
        description="Preprocess NF-002 uncertainty-label data to binary format"
    )
    parser.add_argument(
        "input", help="Input text file (FEN | v_hat | v_star | u | depth | nodes | result)"
    )
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

    data = np.memmap(output_path, dtype=UNC_RECORD_DTYPE, mode="w+", shape=(num_lines,))

    print("Converting to binary format...")
    idx = 0
    skipped = 0
    mismatched_u = 0
    with open(input_path) as f:
        for line in tqdm(f, total=num_lines, desc="Processing", unit="pos"):
            line = line.strip()
            if not line:
                continue

            parts = line.split(" | ")
            if len(parts) != NUM_FIELDS:
                skipped += 1
                continue

            try:
                fen = parts[0].strip()
                v_hat = int(parts[1].strip())
                v_star = int(parts[2].strip())
                u = int(parts[3].strip())
                depth = int(parts[4].strip())
                nodes = int(parts[5].strip())
                result = float(parts[6].strip())
            except ValueError:
                skipped += 1
                continue

            # Self-consistency guard: u must equal v_hat - v_star (the C++ writes
            # all three). A mismatch means a malformed/corrupt row -> recompute
            # rather than trust it, and count it so upstream corruption surfaces.
            if u != v_hat - v_star:
                mismatched_u += 1
                u = v_hat - v_star

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
            record["stm"] = 1 if stm_is_white else 0
            record["v_hat"] = v_hat
            record["v_star"] = v_star
            record["u"] = u
            record["depth"] = min(depth, 255)
            record["nodes"] = min(nodes, np.iinfo(np.uint32).max)
            record["result"] = round(result * 2)

            idx += 1

    data.flush()

    if idx < num_lines:
        with open(output_path, "r+b") as f:
            f.truncate(idx * UNC_RECORD_DTYPE.itemsize)

    file_size = os.path.getsize(output_path)
    print("\nDone:")
    print(f"  Positions: {idx:,} ({skipped:,} skipped)")
    if mismatched_u:
        print(f"  WARNING: {mismatched_u:,} rows had u != v_hat - v_star (recomputed)")
    print(f"  Output: {output_path} ({file_size / 1024 / 1024:.1f} MB)")
    print(f"  Record size: {UNC_RECORD_DTYPE.itemsize} bytes")


if __name__ == "__main__":
    main()
