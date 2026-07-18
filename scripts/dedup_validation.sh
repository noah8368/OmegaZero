#!/usr/bin/env bash
#
# Makes the validation set strictly disjoint from training at the position
# (FEN) level: any position that also occurs anywhere in training is dropped
# from validation, and validation is collapsed to one row per FEN. Training is
# left completely untouched.
#
# Validation positions already come only from dedicated validation games (the
# harness's val_fraction routes whole games to val or train, never both). This
# step adds the remaining guarantee — removing the handful of positions that
# two *different* games happened to reach in common — so there is zero train/val
# leakage at the position level.
#
# Run AFTER combine_runs.sh, on the combined/ dataset. Intended for the Linux
# datagen server (uses GNU `sort -S/-T`); it is disk/RAM heavy at ~100M rows.
#
# Usage:
#   ./scripts/dedup_validation.sh [nnue/data/combined]
#
# Writes validation_data.txt in place; the original is saved alongside as
# validation_data.txt.bak.

set -euo pipefail

DIR="${1:-nnue/data/combined}"
TRAIN="$DIR/training_data.txt"
VAL="$DIR/validation_data.txt"

for f in "$TRAIN" "$VAL"; do
    [[ -f "$f" ]] || { echo "Error: $f not found" >&2; exit 1; }
done

# Keep temp files next to the data — /tmp is often too small for 100M-row sorts.
TMP="$(mktemp -d "$DIR/.dedupval.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C          # byte-wise collation: faster, and consistent across steps

val_before=$(wc -l < "$VAL")

echo "[1/4] extracting + sorting unique training FENs ($(wc -l < "$TRAIN") train rows)..."
# FEN = everything before the first ' | '.
sed 's/ | .*//' "$TRAIN" | sort -u -T "$TMP" -S 60% > "$TMP/train_fens"

echo "[2/4] keying validation by FEN..."
# Emit  FEN <TAB> full_row , sorted by FEN so it can be joined against train.
awk -F' \\| ' '{print $1 "\t" $0}' "$VAL" | sort -t$'\t' -k1,1 -T "$TMP" -S 60% > "$TMP/val_keyed"

echo "[3/4] dropping validation positions that also occur in training..."
# join -v2: rows of val_keyed whose FEN has no match in train_fens.
join -t$'\t' -v 2 -1 1 -2 1 "$TMP/train_fens" "$TMP/val_keyed" | cut -f2- > "$TMP/val_disjoint"

echo "[4/4] collapsing validation to unique FENs..."
awk -F' \\| ' '!seen[$1]++' "$TMP/val_disjoint" > "$TMP/val_final"

cp "$VAL" "$VAL.bak"
mv "$TMP/val_final" "$VAL"

val_after=$(wc -l < "$VAL")
echo ""
echo "Validation: $val_before -> $val_after rows (removed $((val_before - val_after)) leaking/duplicate positions)"
echo "Training:   $(wc -l < "$TRAIN") rows (unchanged)"
echo "Backup:     $VAL.bak"
