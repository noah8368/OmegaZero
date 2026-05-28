#!/usr/bin/env bash
# Combine training and validation data from all datagen runs into the
# combined/ directory. Merges unmerged worker files first, skipping any
# run that's actively in progress.
#
# New run data is merged into the existing combined file with deduplication,
# so it's safe to re-run — no data is lost and no duplicates are added.
# After a run's data is merged, its directory is left intact (not deleted).
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
echo "=== Combining into $COMBINED/ ==="

mkdir -p "$COMBINED"
train_out="$COMBINED/training_data.txt"
val_out="$COMBINED/validation_data.txt"

touch "$train_out" "$val_out"

train_before=$(wc -l < "$train_out" | tr -d ' ')
val_before=$(wc -l < "$val_out" | tr -d ' ')

run_count=0

for run_dir in "$DATA_DIR"/*/; do
    [[ "$(basename "$run_dir")" == "combined" ]] && continue
    [[ -d "$run_dir" ]] || continue

    name="$(basename "$run_dir")"
    added_train=0
    added_val=0

    if [[ -f "$run_dir/training_data.txt" ]]; then
        before=$(wc -l < "$train_out" | tr -d ' ')
        sort -u "$train_out" "$run_dir/training_data.txt" > "$train_out.tmp"
        mv "$train_out.tmp" "$train_out"
        after=$(wc -l < "$train_out" | tr -d ' ')
        added_train=$((after - before))
    fi

    if [[ -f "$run_dir/validation_data.txt" ]]; then
        before=$(wc -l < "$val_out" | tr -d ' ')
        sort -u "$val_out" "$run_dir/validation_data.txt" > "$val_out.tmp"
        mv "$val_out.tmp" "$val_out"
        after=$(wc -l < "$val_out" | tr -d ' ')
        added_val=$((after - before))
    fi

    total_added=$((added_train + added_val))
    if [[ $total_added -gt 0 ]]; then
        printf "  %-30s +%'d train  +%'d val (new unique)\n" "$name" "$added_train" "$added_val"
    else
        printf "  %-30s (no new data)\n" "$name"
    fi
    run_count=$((run_count + 1))
done

train_after=$(wc -l < "$train_out" | tr -d ' ')
val_after=$(wc -l < "$val_out" | tr -d ' ')
grand_total=$((train_after + val_after))

echo ""
printf "Training:   %'d → %'d (+%'d new unique)\n" "$train_before" "$train_after" $((train_after - train_before))
printf "Validation: %'d → %'d (+%'d new unique)\n" "$val_before" "$val_after" $((val_before - val_before))
printf "Total:      %'d positions\n" "$grand_total"

if [[ $grand_total -gt 0 ]]; then
    pct=$(awk "BEGIN { printf \"%.1f\", ($grand_total / 102000000.0) * 100 }")
    printf "Progress:   %s%% of 102M target\n" "$pct"
fi
