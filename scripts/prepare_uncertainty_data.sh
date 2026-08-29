#!/usr/bin/env bash
# Prepare uncertainty (NF-002) training data end to end.
#
# Two steps, one command:
#   1. combine_runs.sh — merge every datagen worker shard under a data dir into
#      deduplicated combined/training_data.txt + combined/validation_data.txt
#      (schema-guarded to uncertainty's 7-field rows; honors the datagen
#      train/val split via data_worker_* vs val_worker_*).
#   2. preprocess_uncertainty.py — encode BOTH split .txt files into the packed
#      .bin (UNC_RECORD_DTYPE) that research/experiments/train_unc_head.py reads.
#
# The result is a ready --train/--val .bin pair. combine_runs.sh dedups and
# preprocessing overwrites, so this is safe to re-run as new datagen lands.
#
# OPTIONAL: train_unc_head.py now auto-encodes .txt -> .bin on its own (like
# train_nnue.py), so the minimal path is just combine_runs.sh then the trainer —
# this wrapper is only a convenience that pre-bakes both .bin ahead of time (e.g.
# to encode once and reuse, or to inspect the .bin before training).
#
# Usage:
#   ./scripts/prepare_uncertainty_data.sh                     # nnue/data_uncertainty
#   ./scripts/prepare_uncertainty_data.sh /path/to/unc_data   # custom data dir
#
# NOTE: like combine_runs.sh, step 1 consumes each run's data_worker_*/
# val_worker_* files (merged into that run's training_data.txt/validation_data.txt,
# then removed) — the positions are preserved, the raw shards are not.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DATA_DIR="${1:-$REPO_ROOT/nnue/data_uncertainty}"
COMBINED="$DATA_DIR/combined"

if [[ ! -d "$DATA_DIR" ]]; then
    echo "Error: data dir not found: $DATA_DIR" >&2
    exit 1
fi

# preprocess_uncertainty.py needs numpy; prefer the repo venv, fall back to python3.
PY="$REPO_ROOT/.venv/bin/python"
[[ -x "$PY" ]] || PY="$(command -v python3)"
if [[ -z "$PY" ]]; then
    echo "Error: no python interpreter found (need one with numpy)." >&2
    exit 1
fi

echo "=== Step 1/2: combine worker shards under $DATA_DIR ==="
"$SCRIPT_DIR/combine_runs.sh" "$DATA_DIR" || exit 1

encode() {  # <split> where split in {training, validation}
    local split="$1"
    local txt="$COMBINED/${split}_data.txt"
    local bin="$COMBINED/${split}_data.bin"
    if [[ ! -s "$txt" ]]; then
        echo "  skip ${split}: $txt is missing or empty"
        return 0
    fi
    echo "=== encode ${split} -> ${bin} ==="
    "$PY" "$SCRIPT_DIR/preprocess_uncertainty.py" "$txt" -o "$bin" || exit 1
}

echo ""
echo "=== Step 2/2: encode combined splits to .bin ==="
encode training
encode validation

echo ""
echo "Done. Fit the uncertainty head with:"
echo "  $PY research/experiments/train_unc_head.py \\"
echo "    --trunk <path/to/net/best.bin> \\"
echo "    --train $COMBINED/training_data.bin \\"
echo "    --val   $COMBINED/validation_data.bin"
