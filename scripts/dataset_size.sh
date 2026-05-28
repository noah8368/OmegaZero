#!/usr/bin/env bash
# Show dataset size (position count) for each datagen run and the combined total.
#
# Usage:
#   ./scripts/dataset_size.sh                  # default: nnue/data
#   ./scripts/dataset_size.sh /path/to/data    # custom data directory

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DATA_DIR="${1:-$REPO_ROOT/nnue/data}"

if [[ ! -d "$DATA_DIR" ]]; then
    echo "Error: $DATA_DIR does not exist"
    exit 1
fi

grand_train=0
grand_val=0

count_lines() {
    if [[ -f "$1" ]]; then
        wc -l < "$1" | tr -d ' '
    else
        echo 0
    fi
}

for run_dir in "$DATA_DIR"/*/; do
    [[ -d "$run_dir" ]] || continue
    name="$(basename "$run_dir")"

    train=0
    val=0

    # Count merged files
    [[ -f "$run_dir/training_data.txt" ]] && train=$(count_lines "$run_dir/training_data.txt")
    [[ -f "$run_dir/validation_data.txt" ]] && val=$(count_lines "$run_dir/validation_data.txt")

    # Count unmerged worker files
    for f in "$run_dir"data_worker_*.txt; do
        [[ -f "$f" ]] && train=$((train + $(count_lines "$f")))
    done
    for f in "$run_dir"val_worker_*.txt; do
        [[ -f "$f" ]] && val=$((val + $(count_lines "$f")))
    done

    total=$((train + val))

    if [[ "$name" == "combined" ]]; then
        echo "--- combined ---"
        printf "  training:   %'d\n" "$train"
        printf "  validation: %'d\n" "$val"
        printf "  total:      %'d\n" "$total"
        echo ""
    else
        grand_train=$((grand_train + train))
        grand_val=$((grand_val + val))
        printf "%-30s %'10d train  %'10d val  %'10d total\n" "$name" "$train" "$val" "$total"
    fi
done

grand_total=$((grand_train + grand_val))
echo ""
echo "=== All runs (excluding combined) ==="
printf "  training:   %'d\n" "$grand_train"
printf "  validation: %'d\n" "$grand_val"
printf "  total:      %'d positions\n" "$grand_total"

if [[ $grand_total -gt 0 ]]; then
    pct=$(awk "BEGIN { printf \"%.1f\", ($grand_total / 102000000.0) * 100 }")
    printf "  progress:   %s%% of 102M target\n" "$pct"
fi
