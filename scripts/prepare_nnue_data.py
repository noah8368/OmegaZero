#!/usr/bin/env python3
"""
prepare_nnue_data.py — one-stop NNUE data prep: combine worker shards, then encode.

Run this once before scripts/train_nnue.py. It chains the two steps that turn raw
datagen output into a ready --train/--val .bin pair:

  1. scripts/combine_runs.sh <data_dir>  — merge every datagen worker shard into
     deduplicated combined/training_data.txt + combined/validation_data.txt.
  2. encode each combined split .txt -> packed .bin (RECORD_DTYPE) that the
     trainer memory-maps.

The NNUE row schema is 3-field (STM POV):  FEN | score | result

This is the NNUE counterpart of scripts/prepare_unc_data.py; both share the same
combine+encode flow (combine_and_encode, defined here and imported by the unc
script) and differ only in the row schema. The importable encoder (encode_nnue,
RECORD_DTYPE, fen_to_halfkp, MAX_FEATURES) is also what scripts/train_nnue.py's
auto-encode path mirrors, so you do NOT strictly need to run this by hand:
train_nnue.py re-encodes a stale/missing .bin from the .txt on its own. Run this
to pre-bake both .bin ahead of time (encode once, reuse) or to inspect them.

Full NNUE flow:
    run_datagen.sh  ->  prepare_nnue_data.py  ->  train_nnue.py

Usage:
    python3 scripts/prepare_nnue_data.py                    # nnue/data
    python3 scripts/prepare_nnue_data.py /path/to/data      # custom data dir
    python3 scripts/prepare_nnue_data.py --skip-combine     # re-encode only
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
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

NUM_FIELDS = 3


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


def encode_nnue(input_path, output_path):
    """Encode a 3-field NNUE .txt into the packed RECORD_DTYPE .bin.

    Returns the number of records written. Importable so callers can (re)encode a
    split outside the full prepare flow."""
    input_path = Path(input_path)
    output_path = Path(output_path)

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
            if len(parts) != NUM_FIELDS:
                skipped += 1
                continue

            try:
                fen = parts[0].strip()
                score = float(parts[1].strip())
                result = float(parts[2].strip())
            except ValueError:
                skipped += 1
                continue

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
    print("\nDone:")
    print(f"  Positions: {idx:,} ({skipped:,} skipped)")
    print(f"  Output: {output_path} ({file_size / 1024 / 1024:.1f} MB)")
    print(f"  Record size: {RECORD_DTYPE.itemsize} bytes")
    return idx


# --------------------------------------------------------------------------- #
#  Shared combine + encode-both-splits flow (also used by prepare_unc_data.py)
# --------------------------------------------------------------------------- #
def run_combine(data_dir):
    """Run scripts/combine_runs.sh over data_dir (merge/dedup worker shards)."""
    script = Path(__file__).resolve().parent / "combine_runs.sh"
    print(f"=== Step 1/2: combine worker shards under {data_dir} ===")
    subprocess.run(["bash", str(script), str(data_dir)], check=True)


def combine_and_encode(data_dir, encode_fn, trainer_hint, skip_combine=False):
    """Combine worker shards, then encode both combined splits to .bin.

    Shared by both pipelines: encode_fn is encode_nnue or encode_uncertainty, and
    trainer_hint is the closing "fit with ..." message. combine_runs.sh dedups and
    encoding overwrites, so this is safe to re-run as new datagen lands."""
    data_dir = Path(data_dir)
    if not data_dir.is_dir():
        sys.exit(f"Error: data dir not found: {data_dir}")
    combined = data_dir / "combined"

    if skip_combine:
        print("=== Step 1/2: combine skipped (--skip-combine) ===")
    else:
        run_combine(data_dir)

    print("\n=== Step 2/2: encode combined splits to .bin ===")
    for split in ("training", "validation"):
        txt = combined / f"{split}_data.txt"
        binp = combined / f"{split}_data.bin"
        if not txt.exists() or txt.stat().st_size == 0:
            print(f"  skip {split}: {txt} is missing or empty")
            continue
        print(f"\n--- encode {split} -> {binp} ---")
        encode_fn(txt, binp)

    print(trainer_hint)


def main():
    parser = argparse.ArgumentParser(
        description="Combine + encode NNUE datagen shards into a ready .bin pair.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    repo_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "data_dir", nargs="?", default=str(repo_root / "nnue" / "data"),
        help="datagen output dir to prepare (default: nnue/data)",
    )
    parser.add_argument(
        "--skip-combine", action="store_true",
        help="skip combine_runs.sh; only (re)encode existing combined/*.txt",
    )
    args = parser.parse_args()

    combined = Path(args.data_dir) / "combined"
    hint = (
        "\nDone. Train the NNUE with:\n"
        f"  python3 scripts/train_nnue.py --data {combined}/training_data.bin"
    )
    combine_and_encode(args.data_dir, encode_nnue, hint, skip_combine=args.skip_combine)


if __name__ == "__main__":
    main()
