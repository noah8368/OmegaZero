#!/usr/bin/env python3
"""
prepare_unc_data.py — one-stop unc-002 uncertainty data prep: combine + encode.

Run this once before research/experiments/train_unc_head.py. It chains the two
steps that turn raw uncertainty datagen output into a ready --train/--val .bin
pair:

  1. scripts/combine_runs.sh <data_dir>  — merge every datagen worker shard into
     deduplicated combined/training_data.txt + combined/validation_data.txt
     (schema-guarded to uncertainty's 7-field rows).
  2. encode each combined split .txt -> packed .bin (UNC_RECORD_DTYPE) that the
     trainer reads.

The uncertainty datagen mode (`mode: uncertainty` in nnue/config.json, see
src/datagen.cc::PlayGameUncertainty) emits a 7-field row per sampled position
(all scores are STM POV, centipawns):

    FEN | v | v_star | u | depth | nodes | result

    v      = raw static eval (Board::Evaluate(), uncorrected)
    v_star = fixed-depth + node-capped deep search score (the target)
    u      = v - v_star, the signed eval error we model  p(u | x)
    depth  = deepest completed depth of the v_star search
    nodes  = nodes visited by the v_star search
    result = game outcome, White POV (0.0 / 0.5 / 1.0)

This is the uncertainty counterpart of scripts/prepare_nnue_data.py; both share
the same combine+encode flow (combine_and_encode, defined there) and the same
HalfKP feature extraction (fen_to_halfkp), so the conditioning input matches the
NNUE trunk exactly. They differ only in the row schema. The importable encoder
(encode_uncertainty, UNC_RECORD_DTYPE) is also what train_unc_head.py's and
generate_unc_head_plots.py's auto-encode paths use, so you do NOT strictly need to run this by
hand: the trainer re-encodes a stale/missing .bin from the .txt on its own. Run
this to pre-bake both .bin ahead of time or to inspect them.

Full uncertainty flow:
    run_datagen.sh (mode: uncertainty)  ->  prepare_unc_data.py  ->  train_unc_head.py

Usage:
    python3 scripts/prepare_unc_data.py                          # nnue/data_uncertainty
    python3 scripts/prepare_unc_data.py /path/to/unc_data        # custom data dir
    python3 scripts/prepare_unc_data.py --skip-combine           # re-encode only
"""

import argparse
import os
import sys
from pathlib import Path

import numpy as np
from tqdm import tqdm

# Reuse the NNUE pipeline's HalfKP feature extraction (so conditioning features
# are byte-for-byte identical to training) and the shared combine+encode flow.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_nnue_data import (  # noqa: E402
    fen_to_halfkp,
    MAX_FEATURES,
    combine_and_encode,
)

# v / v_star / u are int32, NOT int16: unc-002's signal is the fat signed-error
# tail from the KEPT tactical positions (the filters are inverted vs. NNUE datagen),
# and clipping those to +/-32767 would corrupt exactly the tail the margins read.
UNC_RECORD_DTYPE = np.dtype([
    ("num_white", np.uint8),
    ("white_indices", np.uint16, (MAX_FEATURES,)),
    ("num_black", np.uint8),
    ("black_indices", np.uint16, (MAX_FEATURES,)),
    ("stm", np.uint8),       # 0=black, 1=white
    ("v", np.int32),         # raw static eval, STM POV (centipawns)
    ("v_star", np.int32),    # fixed-depth search target, STM POV (centipawns)
    ("u", np.int32),         # signed error v - v_star (the modeled quantity)
    ("depth", np.uint8),     # deepest completed depth of the v_star search
    ("nodes", np.uint32),    # nodes visited by the v_star search
    ("result", np.uint8),    # 0=loss(0.0), 1=draw(0.5), 2=win(1.0), White POV
])

NUM_FIELDS = 7


def encode_uncertainty(input_path, output_path):
    """Encode a 7-field uncertainty .txt into the packed UNC_RECORD_DTYPE .bin.

    Returns the number of records written. Importable so the trainer
    (research/experiments/train_unc_head.py) and generate_unc_head_plots.py can (re)encode on
    staleness in the same way the NNUE pipeline does."""
    input_path = Path(input_path)
    output_path = Path(output_path)

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
                v = int(parts[1].strip())
                v_star = int(parts[2].strip())
                u = int(parts[3].strip())
                depth = int(parts[4].strip())
                nodes = int(parts[5].strip())
                result = float(parts[6].strip())
            except ValueError:
                skipped += 1
                continue

            # Self-consistency guard: u must equal v - v_star (the C++ writes
            # all three). A mismatch means a malformed/corrupt row -> recompute
            # rather than trust it, and count it so upstream corruption surfaces.
            if u != v - v_star:
                mismatched_u += 1
                u = v - v_star

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
            record["v"] = v
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
        print(f"  WARNING: {mismatched_u:,} rows had u != v - v_star (recomputed)")
    print(f"  Output: {output_path} ({file_size / 1024 / 1024:.1f} MB)")
    print(f"  Record size: {UNC_RECORD_DTYPE.itemsize} bytes")
    return idx


def main():
    parser = argparse.ArgumentParser(
        description="Combine + encode uncertainty datagen shards into a ready .bin pair.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    repo_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "data_dir", nargs="?",
        default=str(repo_root / "nnue" / "data_uncertainty"),
        help="datagen output dir to prepare (default: nnue/data_uncertainty)",
    )
    parser.add_argument(
        "--skip-combine", action="store_true",
        help="skip combine_runs.sh; only (re)encode existing combined/*.txt",
    )
    args = parser.parse_args()

    combined = Path(args.data_dir) / "combined"
    hint = (
        "\nDone. Fit the uncertainty head with:\n"
        "  python3 research/experiments/train_unc_head.py \\\n"
        "    --trunk <path/to/net/best.bin> \\\n"
        f"    --train {combined}/training_data.bin \\\n"
        f"    --val   {combined}/validation_data.bin"
    )
    combine_and_encode(args.data_dir, encode_uncertainty, hint,
                       skip_combine=args.skip_combine)


if __name__ == "__main__":
    main()
