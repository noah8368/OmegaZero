#!/usr/bin/env bash
# Combine training and validation data from all datagen runs into a single
# directory. Safe to re-run — overwrites the combined files each time.
#
# Usage:
#   ./scripts/combine_runs.sh                  # default: nnue/data
#   ./scripts/combine_runs.sh /path/to/data    # custom data directory

set -uo pipefail

DATA_DIR="${1:-nnue/data}"
COMBINED="$DATA_DIR/combined"

mkdir -p "$COMBINED"

train_out="$COMBINED/training_data.txt"
val_out="$COMBINED/validation_data.txt"

> "$train_out"
> "$val_out"

train_total=0
val_total=0

for run_dir in "$DATA_DIR"/*/; do
    [[ "$(basename "$run_dir")" == "combined" ]] && continue

    if [[ -f "$run_dir/training_data.txt" ]]; then
        count=$(wc -l < "$run_dir/training_data.txt")
        train_total=$((train_total + count))
        cat "$run_dir/training_data.txt" >> "$train_out"
    fi

    if [[ -f "$run_dir/validation_data.txt" ]]; then
        count=$(wc -l < "$run_dir/validation_data.txt")
        val_total=$((val_total + count))
        cat "$run_dir/validation_data.txt" >> "$val_out"
    fi
done

total=$((train_total + val_total))

echo "Combined into: $COMBINED/"
echo "  Training:   $train_total positions"
echo "  Validation: $val_total positions"
echo "  Total:      $total positions"
