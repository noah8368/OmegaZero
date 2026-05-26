#!/usr/bin/env bash
# Sync datagen output to a remote machine (e.g. laptop) twice daily.
# Sends an email status report after each sync.
#
# Usage:
#   ./scripts/sync_datagen.sh user@laptop:~/OmegaZero/nnue/data/ --email noahhimed1@gmail.com
#   ./scripts/sync_datagen.sh user@laptop:~/OmegaZero/nnue/data/ --email noahhimed1@gmail.com --name epyc-1 --games 1700000
#
# Requires GMAIL_APP_PASSWORD env var for email notifications.

set -euo pipefail

DEST="${1:?Usage: $0 <user@host:/path/to/dest/> [options]}"
INTERVAL=43200  # 12 hours = 2x/day
DATA_DIR="nnue/data"
EMAIL=""
NAME=""
TOTAL_GAMES=0

shift
while [[ $# -gt 0 ]]; do
    case "$1" in
        --interval) INTERVAL="$2"; shift 2 ;;
        --data-dir) DATA_DIR="$2"; shift 2 ;;
        --email) EMAIL="$2"; shift 2 ;;
        --name) NAME="$2"; shift 2 ;;
        --games) TOTAL_GAMES="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

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
