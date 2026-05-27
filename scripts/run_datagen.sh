#!/usr/bin/env bash
# Watchdog + sync for datagen. Monitors the datagen process, syncs data
# periodically, and restarts on crash.
#
# Handles three exit scenarios:
#   Exit 0:          Clean completion — sync and stop.
#   Exit 2:          Worker fatal crash — harness already synced and emailed.
#                    Watchdog counts remaining games, restarts.
#   Other / signal:  Process-level crash (SIGABRT, segfault). Watchdog writes
#                    crash log, syncs, emails, restarts.
#   SIGTERM/SIGINT:  Intentional stop — harness handles its own email.
#                    Watchdog syncs and stops (does NOT restart).
#
# Usage:
#   ./scripts/run_datagen.sh              # start datagen + watchdog
#   ./scripts/run_datagen.sh <PID>        # attach to existing datagen PID
#
# All settings read from nnue/config.json.

set -uo pipefail

CONFIG="nnue/config.json"
if [[ ! -f "$CONFIG" ]]; then
    echo "Error: $CONFIG not found."
    exit 1
fi

read_cfg() { python3 -c "import json; c=json.load(open('$CONFIG')); print(c.get('$1','$2'))" ; }

DEST=$(read_cfg sync_dest "")
INTERVAL=$(read_cfg sync_interval 43200)
DATA_DIR=$(read_cfg output "nnue/data")
EMAIL=$(read_cfg email "")
NAME=$(read_cfg name "")
TOTAL_GAMES=$(read_cfg games 0)

MAX_RESTARTS=10
POLL_INTERVAL="${WATCHDOG_POLL_INTERVAL:-30}"

tag="${NAME:+ [$NAME]}"

send_email() {
    local subject="$1" body="$2"
    if [[ -n "$EMAIL" ]]; then
        python3 scripts/send_email.py \
            --to "$EMAIL" \
            --subject "$subject" \
            --body "$body" 2>/dev/null || echo "  Email send failed"
    fi
}

do_sync() {
    if [[ -z "$DEST" ]]; then
        return 0
    fi
    local ts
    ts=$(date "+%Y-%m-%d %H:%M:%S")
    echo "[$ts] Merging worker files..."
    ./scripts/merge_workers.sh "$DATA_DIR" 2>&1 || echo "  Merge had warnings (non-fatal)"

    echo "[$ts] Syncing $DATA_DIR → $DEST..."
    if rsync -avz --progress --partial "$DATA_DIR/" "$DEST"; then
        echo "[$ts] Sync complete."
        return 0
    else
        echo "[$ts] Sync FAILED (rsync exit $?)."
        return 1
    fi
}

count_completed_games() {
    local count=0
    for f in "$DATA_DIR"/*/metadata.txt; do
        [[ -f "$f" ]] || continue
        local g
        g=$(grep -m1 "^games:" "$f" 2>/dev/null | awk '{print $2}') || true
        [[ -n "$g" ]] && count=$((count + g))
    done
    echo "$count"
}

count_positions() {
    local train=0 val=0
    for f in "$DATA_DIR"/*/training_data.txt; do
        [[ -f "$f" ]] && train=$((train + $(wc -l < "$f")))
    done
    for f in "$DATA_DIR"/*/validation_data.txt; do
        [[ -f "$f" ]] && val=$((val + $(wc -l < "$f")))
    done
    echo "$((train + val))"
}

get_eta() {
    local positions=$1
    local elapsed=$2
    python3 -c "
positions = $positions
elapsed = $elapsed
total_games = $TOTAL_GAMES
if positions == 0 or elapsed == 0 or total_games == 0:
    exit(0)
est_games = positions / 17.0
if est_games <= 0:
    exit(0)
rate = est_games / elapsed
remaining = max(0, total_games - est_games)
eta_sec = int(remaining / rate) if rate > 0 else 0
s = eta_sec
parts = []
mo = s // (30*24*3600); s %= 30*24*3600
wk = s // (7*24*3600); s %= 7*24*3600
dy = s // (24*3600); s %= 24*3600
hr = s // 3600; s %= 3600
mn = s // 60; sc = s % 60
if mo: parts.append(f'{mo}mo')
if wk: parts.append(f'{wk}wk')
if dy: parts.append(f'{dy}dy')
if hr: parts.append(f'{hr}hr')
if mn: parts.append(f'{mn}min')
if sc or not parts: parts.append(f'{sc}s')
from datetime import datetime, timedelta
dur = ' '.join(parts)
eta_date = (datetime.now() + timedelta(seconds=eta_sec)).strftime('%Y-%m-%d %H:%M:%S')
print(f'Time remaining: {dur}')
print(f'ETA: {eta_date}')
" 2>/dev/null || true
}

write_crash_log() {
    local reason="${1:-unknown}"
    local ts
    ts=$(date "+%Y-%m-%d %H:%M:%S")
    local completed
    completed=$(count_completed_games)
    local positions
    positions=$(count_positions)

    # Find the most recent run directory for the crash log
    local latest_run
    latest_run=$(ls -dt "$DATA_DIR"/*/ 2>/dev/null | head -1)
    local log_path="${latest_run}crash_log.txt"

    local entry="=== CRASH LOG ===
Timestamp: $ts
Type: process_crash
Reason: $reason
Games completed (all runs): $completed / $TOTAL_GAMES
Positions generated (all runs): $positions
Action: Watchdog syncing data and restarting
=================
"
    echo "$entry" >> "$log_path"
    echo "$entry"
}

DATAGEN_BIN="./build/datagen_harness"

start_datagen() {
    if [[ ! -x "$DATAGEN_BIN" ]]; then
        echo "Error: $DATAGEN_BIN not found. Run 'make datagen' first."
        exit 1
    fi
    echo "Starting datagen harness..."
    "$DATAGEN_BIN" >> datagen.log 2>&1 &
    DATAGEN_PID=$!
    echo "  PID: $DATAGEN_PID"
}

# --- Main ---

restart_count=0

if [[ $# -ge 1 ]]; then
    DATAGEN_PID="$1"
    if ! kill -0 "$DATAGEN_PID" 2>/dev/null; then
        echo "Error: PID $DATAGEN_PID is not running."
        exit 1
    fi
    echo "Attaching to existing datagen PID $DATAGEN_PID"
else
    start_datagen
fi

GAMES=$(read_cfg games 0)
WORKERS=$(read_cfg workers 1)
ST=$(read_cfg st 0.5)

echo ""
echo "=== Datagen Watchdog ==="
echo "  Games: $GAMES"
echo "  Workers: $WORKERS"
echo "  Search time: ${ST}s/move"
echo "  Output: $DATA_DIR"
echo "  Sync dest: ${DEST:-(none)}"
[[ -n "$DEST" ]] && echo "  Sync interval: $((INTERVAL / 3600))h $((INTERVAL % 3600 / 60))m"
echo "  Email: ${EMAIL:-(none)}"
echo "  Max restarts: $MAX_RESTARTS"
echo "  Poll interval: ${POLL_INTERVAL}s"
echo "  Watchdog PID: $$"
echo "  Datagen PID: $DATAGEN_PID"
echo ""
echo "Logs: datagen.log  |  Kill watchdog: kill $$"
echo ""

sync_count=0
last_sync_time=$(date +%s)
watchdog_start_time=$(date +%s)

while true; do
    # Check if datagen is still running
    if ! kill -0 "$DATAGEN_PID" 2>/dev/null; then
        ts=$(date "+%Y-%m-%d %H:%M:%S")

        # Read .exit_status from the latest run directory.
        # The harness writes "complete", "restart", or "shutdown" before exiting.
        # If the file is missing, the process was killed (SIGABRT, segfault, etc.).
        latest_run=$(ls -dt "$DATA_DIR"/*/ 2>/dev/null | head -1)
        status_file="${latest_run}.exit_status"
        exit_status=""
        if [[ -n "$latest_run" && -f "$status_file" ]]; then
            exit_status=$(head -1 "$status_file")
            rm -f "$status_file"
        fi

        echo "[$ts] Datagen PID $DATAGEN_PID exited (status: ${exit_status:-missing})"

        if [[ "$exit_status" == "complete" ]]; then
            echo "  Clean completion. Final sync..."
            do_sync
            echo "Watchdog exiting."
            exit 0
        fi

        if [[ "$exit_status" == "shutdown" ]]; then
            echo "  Intentional stop (SIGTERM/SIGINT). Syncing and exiting..."
            do_sync
            echo "Watchdog exiting."
            exit 0
        fi

        if [[ "$exit_status" == "restart" ]]; then
            echo "  Worker fatal crash — harness already emailed and synced."
        else
            echo "  Process crash (no exit status file — killed by signal)"
            crash_entry=$(write_crash_log "no .exit_status — killed by signal")
            echo "  Syncing crash data..."
            do_sync
            send_email \
                "OmegaZero${tag} PROCESS CRASH — watchdog restarting" \
                "$crash_entry"
        fi

        # Check restart budget
        restart_count=$((restart_count + 1))
        if [[ "$restart_count" -ge "$MAX_RESTARTS" ]]; then
            echo "  Max restarts ($MAX_RESTARTS) reached. Giving up."
            send_email \
                "OmegaZero${tag} WATCHDOG STOPPED — max restarts reached" \
                "Datagen crashed $MAX_RESTARTS times. Manual intervention required.
Exit status: ${exit_status:-missing}
Data synced to: ${DEST:-(no sync dest)}"
            exit 1
        fi

        # Count remaining games and update config
        completed=$(count_completed_games)
        remaining=$((TOTAL_GAMES - completed))
        if [[ "$remaining" -le 0 ]]; then
            echo "  All $TOTAL_GAMES games already completed. Exiting."
            exit 0
        fi

        echo "  Completed: $completed / $TOTAL_GAMES — restarting with $remaining remaining"
        python3 -c "
import json
with open('$CONFIG') as f:
    cfg = json.load(f)
cfg['games'] = $remaining
with open('$CONFIG', 'w') as f:
    json.dump(cfg, f, indent=2)
"
        start_datagen
        last_sync_time=$(date +%s)
        continue
    fi

    # Periodic sync while datagen is running
    now=$(date +%s)
    elapsed=$((now - last_sync_time))
    if [[ "$elapsed" -ge "$INTERVAL" ]]; then
        sync_count=$((sync_count + 1))
        ts=$(date "+%Y-%m-%d %H:%M:%S")
        echo ""
        echo "[$ts] Periodic sync #$sync_count"
        do_sync

        positions=$(count_positions)
        pct_line=""
        eta_line=""
        if [[ "$TOTAL_GAMES" -gt 0 ]]; then
            est_games=$((positions * 100 / 15 / TOTAL_GAMES))
            [[ "$est_games" -gt 100 ]] && est_games=100
            pct_line="Estimated progress: ~${est_games}%"
            wd_elapsed=$((now - watchdog_start_time))
            eta_line=$(get_eta "$positions" "$wd_elapsed")
        fi

        body="Sync #$sync_count complete at $ts
${NAME:+Machine: $NAME
}Total positions: $positions
$pct_line
${eta_line:+$eta_line
}Restarts so far: $restart_count"

        send_email "OmegaZero${tag} sync #$sync_count — ${positions} positions" "$body"

        last_sync_time=$now
        echo ""
    fi

    sleep "$POLL_INTERVAL"
done
