#!/usr/bin/env bash
# Sync datagen output to a remote machine (e.g. laptop) twice daily.
# Sends an email status report after each sync.
# All settings read from nnue/config.json.
#
# Usage:
#   ./scripts/sync_datagen.sh

set -euo pipefail

CONFIG="nnue/config.json"
if [[ ! -f "$CONFIG" ]]; then
    echo "Error: $CONFIG not found. Copy nnue/config.json.example and fill it in."
    exit 1
fi

read_cfg() { python3 -c "import json; c=json.load(open('$CONFIG')); print(c.get('$1','$2'))" ; }

DEST=$(read_cfg sync_dest "")
INTERVAL=$(read_cfg sync_interval 43200)
DATA_DIR=$(read_cfg output "nnue/data")
EMAIL=$(read_cfg email "")
NAME=$(read_cfg name "")
TOTAL_GAMES=$(read_cfg games 0)

if [[ -z "$DEST" ]]; then
    echo "Error: sync_dest not set in $CONFIG (e.g. \"user@laptop:~/OmegaZero/nnue/data/\")"
    exit 1
fi

if [[ ! -d "$DATA_DIR" ]]; then
    echo "Error: $DATA_DIR does not exist. Is datagen running?"
    exit 1
fi

echo "Syncing $DATA_DIR → $DEST every $((INTERVAL / 3600))h $((INTERVAL % 3600 / 60))m"
[[ -n "$EMAIL" ]] && echo "Email reports → $EMAIL"
echo "PID: $$  (kill $$ to stop)"
echo ""

sync_count=0
while true; do
    sync_count=$((sync_count + 1))
    ts=$(date "+%Y-%m-%d %H:%M:%S")

    echo "[$ts] Sync #$sync_count starting..."
    if rsync -avz --progress --partial "$DATA_DIR/" "$DEST"; then
        echo "[$ts] Sync #$sync_count complete."
    else
        echo "[$ts] Sync #$sync_count FAILED (rsync exit $?). Will retry next cycle."
    fi

    # Count positions across all worker and combined files
    train_positions=0
    val_positions=0
    for f in "$DATA_DIR"/*/training_data.txt "$DATA_DIR"/*/data_worker_*.txt; do
        [[ -f "$f" ]] && train_positions=$((train_positions + $(wc -l < "$f")))
    done
    for f in "$DATA_DIR"/*/validation_data.txt "$DATA_DIR"/*/val_worker_*.txt; do
        [[ -f "$f" ]] && val_positions=$((val_positions + $(wc -l < "$f")))
    done
    total_positions=$((train_positions + val_positions))

    # Build status body
    tag="${NAME:+ [$NAME]}"
    pct_line=""
    if [[ "$TOTAL_GAMES" -gt 0 ]]; then
        # Estimate games from positions (~15 pos/game)
        est_games=$((total_positions * 100 / 15 / TOTAL_GAMES))
        [[ "$est_games" -gt 100 ]] && est_games=100
        remaining=$((100 - est_games))
        pct_line="Estimated progress: ~${est_games}% done, ~${remaining}% remaining"
    fi

    body="Sync #$sync_count complete at $ts
${NAME:+Machine: $NAME
}Training positions: $train_positions
Validation positions: $val_positions
Total positions: $total_positions
${pct_line}"

    echo "$body"
    echo ""

    if [[ -n "$EMAIL" ]]; then
        python3 scripts/send_email.py \
            --to "$EMAIL" \
            --subject "OmegaZero${tag} sync #$sync_count — ${total_positions} positions" \
            --body "$body" 2>&1 || echo "Email send failed"
    fi

    echo "Next sync in $((INTERVAL / 3600))h $((INTERVAL % 3600 / 60))m"
    echo ""
    sleep "$INTERVAL"
done
