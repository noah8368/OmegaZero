#!/usr/bin/env bash
# Watchdog for datagen. Monitors the datagen process and restarts on crash.
#
# Handles three exit scenarios:
#   Exit 0:          Clean completion — stop.
#   Exit 2:          Worker fatal crash — harness already emailed.
#                    Watchdog counts remaining games, restarts.
#   Other / signal:  Process-level crash (SIGABRT, segfault). Watchdog writes
#                    crash log, emails, restarts.
#   SIGTERM/SIGINT:  Intentional stop — harness handles its own email.
#                    Watchdog stops (does NOT restart).
#
# Usage:
#   ./scripts/run_datagen.sh              # start datagen + watchdog
#   ./scripts/run_datagen.sh <PID>        # attach to existing datagen PID
#
# All settings read from nnue/config.json.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CONFIG="$REPO_ROOT/nnue/config.json"
if [[ ! -f "$CONFIG" ]]; then
    echo "Error: $CONFIG not found."
    exit 1
fi

read_cfg() { python3 -c "import json; c=json.load(open('$CONFIG')); print(c.get('$1','$2'))" ; }

DATA_DIR=$(read_cfg output "$REPO_ROOT/nnue/data")
EMAIL=$(read_cfg email "")
NAME=$(read_cfg name "")
TOTAL_GAMES=$(read_cfg games 0)

MAX_RESTARTS=10
POLL_INTERVAL="${WATCHDOG_POLL_INTERVAL:-30}"

tag="${NAME:+ [$NAME]}"

send_email() {
    local subject="$1" body="$2"
    if [[ -n "$EMAIL" ]]; then
        python3 "$SCRIPT_DIR/send_email.py" \
            --to "$EMAIL" \
            --subject "$subject" \
            --body "$body" 2>/dev/null || echo "  Email send failed"
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
Action: Watchdog restarting
=================
"
    echo "$entry" >> "$log_path"
    echo "$entry"
}

DATAGEN_BIN="$REPO_ROOT/build/datagen_harness"

start_datagen() {
    if [[ ! -x "$DATAGEN_BIN" ]]; then
        echo "Error: $DATAGEN_BIN not found. Run 'make datagen' first."
        exit 1
    fi
    echo "Starting datagen harness..."
    "$DATAGEN_BIN" >> "$REPO_ROOT/datagen.log" 2>&1 &
    DATAGEN_PID=$!
    echo "  PID: $DATAGEN_PID"
}

# --- Main ---

restart_count=0

# Check for already-running datagen_harness processes
existing_pids=$(pgrep -f datagen_harness 2>/dev/null | grep -v "$$" || true)
if [[ -n "$existing_pids" ]]; then
    echo "ERROR: datagen_harness is already running (PIDs: $existing_pids)"
    echo "Kill it first:  kill $existing_pids"
    echo "Or attach:      $0 <PID>"
    exit 1
fi

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
echo "  Email: ${EMAIL:-(none)}"
echo "  Max restarts: $MAX_RESTARTS"
echo "  Poll interval: ${POLL_INTERVAL}s"
echo "  Watchdog PID: $$"
echo "  Datagen PID: $DATAGEN_PID"
echo ""
echo "Logs: $REPO_ROOT/datagen.log  |  Kill watchdog: kill $$"
echo ""

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
            echo "  Clean completion."
            echo "Watchdog exiting."
            exit 0
        fi

        if [[ "$exit_status" == "shutdown" ]]; then
            echo "  Intentional stop (SIGTERM/SIGINT)."
            echo "Watchdog exiting."
            exit 0
        fi

        if [[ "$exit_status" == "restart" ]]; then
            echo "  Worker fatal crash — harness already emailed."
        else
            echo "  Process crash (no exit status file — killed by signal)"
            echo "  Merging worker files..."
            "$SCRIPT_DIR/merge_workers.sh" "$DATA_DIR" 2>&1 || echo "  Merge had warnings (non-fatal)"
            crash_entry=$(write_crash_log "no .exit_status — killed by signal")
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
Exit status: ${exit_status:-missing}"
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
        continue
    fi

    sleep "$POLL_INTERVAL"
done
