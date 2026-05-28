#!/usr/bin/env bash
# Merge per-worker data files into combined training_data.txt / validation_data.txt.
# Safe to run while datagen is still writing — appends new data each time.
#
# Usage:
#   ./scripts/merge_workers.sh nnue/data
#   ./scripts/merge_workers.sh  (reads output dir from nnue/config.json)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CONFIG="$REPO_ROOT/nnue/config.json"

if [[ $# -ge 1 ]]; then
    DATA_DIR="$1"
else
    if [[ -f "$CONFIG" ]]; then
        DATA_DIR=$(python3 -c "import json; print(json.load(open('$CONFIG')).get('output','$REPO_ROOT/nnue/data'))")
    else
        DATA_DIR="$REPO_ROOT/nnue/data"
    fi
fi

if [[ ! -d "$DATA_DIR" ]]; then
    echo "Error: $DATA_DIR does not exist"
    exit 1
fi

merged=0
for run_dir in "$DATA_DIR"/*/; do
    [[ -d "$run_dir" ]] || continue

    train_files=("$run_dir"data_worker_*.txt)
    val_files=("$run_dir"val_worker_*.txt)

    has_workers=false
    for f in "${train_files[@]}" "${val_files[@]}"; do
        [[ -f "$f" ]] && has_workers=true && break
    done
    $has_workers || continue

    train_combined="$run_dir/training_data.txt"
    val_combined="$run_dir/validation_data.txt"

    # Append worker files into combined (don't clobber existing combined data)
    for f in "${train_files[@]}"; do
        [[ -f "$f" ]] && cat "$f" >> "$train_combined" && rm "$f"
    done
    for f in "${val_files[@]}"; do
        [[ -f "$f" ]] && cat "$f" >> "$val_combined" && rm "$f"
    done

    train_count=0
    val_count=0
    [[ -f "$train_combined" ]] && train_count=$(wc -l < "$train_combined")
    [[ -f "$val_combined" ]] && val_count=$(wc -l < "$val_combined")

    echo "$(basename "$run_dir"): $train_count training, $val_count validation positions"
    merged=$((merged + 1))
done

if [[ $merged -eq 0 ]]; then
    echo "No worker files to merge in $DATA_DIR"
fi
