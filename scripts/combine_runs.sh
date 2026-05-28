#!/usr/bin/env bash
# Combine training and validation data from all datagen runs into a single
# directory. Automatically merges unmerged worker files first, skipping any
# run that's actively in progress. Safe to re-run — overwrites the combined
# files each time.
#
# Usage:
#   ./scripts/combine_runs.sh                  # default: nnue/data
#   ./scripts/combine_runs.sh /path/to/data    # custom data directory

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DATA_DIR="${1:-$REPO_ROOT/nnue/data}"
COMBINED="$DATA_DIR/combined"

is_run_active() {
    local run_dir="$1"
    if ! pgrep -f datagen_harness > /dev/null 2>&1; then
        return 1
    fi
    for f in "$run_dir"data_worker_*.txt "$run_dir"val_worker_*.txt; do
        [[ -f "$f" ]] || continue
        if lsof "$f" > /dev/null 2>&1; then
            return 0
        fi
    done
    return 1
}

echo "=== Merging unmerged worker files ==="
merged_runs=0
skipped_runs=0

for run_dir in "$DATA_DIR"/*/; do
    [[ "$(basename "$run_dir")" == "combined" ]] && continue
    [[ -d "$run_dir" ]] || continue

    has_workers=false
    for f in "$run_dir"data_worker_*.txt "$run_dir"val_worker_*.txt; do
        [[ -f "$f" ]] && has_workers=true && break
    done
    $has_workers || continue

    if is_run_active "$run_dir"; then
        echo "  SKIP $(basename "$run_dir") — run in progress"
        skipped_runs=$((skipped_runs + 1))
        continue
    fi

    for f in "$run_dir"data_worker_*.txt; do
        [[ -f "$f" ]] && cat "$f" >> "$run_dir/training_data.txt" && rm "$f"
    done
    for f in "$run_dir"val_worker_*.txt; do
        [[ -f "$f" ]] && cat "$f" >> "$run_dir/validation_data.txt" && rm "$f"
    done

    train_count=0
    val_count=0
    [[ -f "$run_dir/training_data.txt" ]] && train_count=$(wc -l < "$run_dir/training_data.txt")
    [[ -f "$run_dir/validation_data.txt" ]] && val_count=$(wc -l < "$run_dir/validation_data.txt")
    echo "  MERGED $(basename "$run_dir"): $train_count training, $val_count validation"
    merged_runs=$((merged_runs + 1))
done

if [[ $merged_runs -eq 0 && $skipped_runs -eq 0 ]]; then
    echo "  No unmerged worker files found."
fi

echo ""
echo "=== Combining all runs ==="

mkdir -p "$COMBINED"
train_out="$COMBINED/training_data.txt"
val_out="$COMBINED/validation_data.txt"

train_tmp=$(mktemp)
val_tmp=$(mktemp)
trap 'rm -f "$train_tmp" "$val_tmp"' EXIT

# Preserve existing combined data as the base
[[ -f "$train_out" ]] && cat "$train_out" > "$train_tmp"
[[ -f "$val_out" ]] && cat "$val_out" > "$val_tmp"

new_train=0
new_val=0

for run_dir in "$DATA_DIR"/*/; do
    [[ "$(basename "$run_dir")" == "combined" ]] && continue

    if [[ -f "$run_dir/training_data.txt" ]]; then
        count=$(wc -l < "$run_dir/training_data.txt")
        new_train=$((new_train + count))
        cat "$run_dir/training_data.txt" >> "$train_tmp"
    fi

    if [[ -f "$run_dir/validation_data.txt" ]]; then
        count=$(wc -l < "$run_dir/validation_data.txt")
        new_val=$((new_val + count))
        cat "$run_dir/validation_data.txt" >> "$val_tmp"
    fi
done

mv "$train_tmp" "$train_out"
mv "$val_tmp" "$val_out"

train_total=$(wc -l < "$train_out")
val_total=$(wc -l < "$val_out")
total=$((train_total + val_total))

echo "Combined into: $COMBINED/"
echo "  Existing + new training:   $train_total positions (+$new_train new)"
echo "  Existing + new validation: $val_total positions (+$new_val new)"
echo "  Total:      $total positions"
