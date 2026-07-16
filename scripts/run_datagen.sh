#!/usr/bin/env bash
# Watchdog for NNUE datagen. Launches datagen_harness, monitors for crashes,
# and automatically restarts on failure (up to 10 times). The harness plays
# OmegaZero against itself using direct engine calls (no UCI overhead). Each
# position's FEN, search score, and game outcome are recorded.
#
# Usage:
#   ./scripts/run_datagen.sh              # start datagen + watchdog
#   ./scripts/run_datagen.sh <PID>        # attach to existing datagen PID
#   nohup ./scripts/run_datagen.sh > datagen.log 2>&1 &   # background on server
#
# Graceful shutdown:
#   ./scripts/shutdown_datagen.sh
#   Workers finish their current game, flush data, merge worker files, and
#   write metadata. Typically takes 30-60s.
#
# Config (nnue/config.json — copy from nnue/config.json.example):
#   games              Total self-play games                       (default: 100)
#   st                 Search time per move in seconds             (default: 0.5)
#   workers            Parallel threads                            (default: 1)
#   output             Output directory                            (default: nnue/data)
#   val_fraction       Fraction of games for validation            (default: 0.1)
#   email              Email for notifications (empty = disabled)  (default: "")
#   name               Machine identifier for email subjects       (default: "")
#   gmail_app_password Gmail app password for sending email         (default: "")
#
# Quality filters (applied automatically by datagen_harness):
#   - Positions in check, mate scores, and |score| > 3000cp are skipped
#   - Every 4th eligible position is sampled
#   - Zobrist hash deduplication within each worker
#   - Games adjudicated at 1000cp for 5 consecutive moves
#   - First 10 plies skipped (opening theory)
#   - Each game starts with 8 random moves for opening diversity
#
# Output (timestamped subdirectory, e.g. nnue/data/2026-05-25_20-33-09_eaf5059/):
#   training_data.txt   — training positions
#   validation_data.txt — validation positions (from separate games)
#   metadata.txt        — generation parameters, timestamp, git commit
#   crash_log.txt       — structured crash entries (only if crashes occurred)
#
# Crash handling (four tiers):
#   1. Per-game crash    — worker logs to crash_log.txt, emails, continues
#   2. Consecutive limit — 5 crashes in a row → all workers stop, watchdog restarts
#   3. Worker fatal      — unrecoverable exception → all workers stop, watchdog restarts
#   4. Process crash     — SIGABRT/segfault → watchdog writes crash log, emails, restarts
#
# Email notifications (when 'email' is set in config):
#   Startup (after 10 games), completion, heartbeat (12h), milestones (10%),
#   crashes (log contents in body), shutdown (progress summary).
#
# Environment:
#   WATCHDOG_POLL_INTERVAL  Seconds between liveness checks (default: 30)

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

# Persist the original total so restarts of the watchdog itself don't lose it.
TOTAL_GAMES_FILE="$DATA_DIR/.total_games"
if [[ -f "$TOTAL_GAMES_FILE" ]]; then
    TOTAL_GAMES=$(cat "$TOTAL_GAMES_FILE")
else
    TOTAL_GAMES=$(read_cfg games 0)
    mkdir -p "$DATA_DIR"
    echo "$TOTAL_GAMES" > "$TOTAL_GAMES_FILE"
fi

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
    for run_dir in "$DATA_DIR"/*/; do
        [[ -d "$run_dir" ]] || continue
        local meta="$run_dir/metadata.txt"
        if [[ -f "$meta" ]]; then
            local g
            g=$(grep -m1 "^games:" "$meta" 2>/dev/null | awk '{print $2}') || true
            [[ -n "$g" ]] && count=$((count + g))
        else
            # No metadata — estimate games from position count (~17 pos/game).
            local positions=0
            for f in "$run_dir"training_data.txt "$run_dir"data_worker_*.txt; do
                [[ -f "$f" ]] && positions=$((positions + $(wc -l < "$f")))
            done
            for f in "$run_dir"validation_data.txt "$run_dir"val_worker_*.txt; do
                [[ -f "$f" ]] && positions=$((positions + $(wc -l < "$f")))
            done
            if [[ "$positions" -gt 0 ]]; then
                count=$((count + positions / 17))
            fi
        fi
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
    # Pass cumulative campaign context so milestone/heartbeat/ETA emails reflect
    # overall progress across restarts, not just this process's batch (the
    # harness reads OZ_TOTAL_GAMES / OZ_BASE_COMPLETED; see datagen.cc).
    OZ_TOTAL_GAMES="$TOTAL_GAMES" OZ_BASE_COMPLETED="$(count_completed_games)" \
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
            # Restore original game count in config and remove tracking file.
            python3 -c "
import json
with open('$CONFIG') as f:
    cfg = json.load(f)
cfg['games'] = $TOTAL_GAMES
with open('$CONFIG', 'w') as f:
    json.dump(cfg, f, indent=2)
"
            rm -f "$TOTAL_GAMES_FILE"
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
