#!/usr/bin/env bash
# Graceful shutdown of the datagen pipeline (watchdog + harness).
# Workers finish their current game, flush files, merge, and write metadata.
# Typically takes 30-60s depending on search time.

set -uo pipefail

WATCHDOG_PID=$(pgrep -f run_datagen.sh 2>/dev/null || true)
HARNESS_PID=$(pgrep -f datagen_harness 2>/dev/null || true)

if [[ -z "$WATCHDOG_PID" && -z "$HARNESS_PID" ]]; then
    echo "Nothing running."
    exit 0
fi

if [[ -n "$WATCHDOG_PID" ]]; then
    echo "Stopping watchdog (PID $WATCHDOG_PID)..."
    kill $WATCHDOG_PID 2>/dev/null
fi

if [[ -n "$HARNESS_PID" ]]; then
    echo "Stopping harness (PID $HARNESS_PID)... waiting for workers to finish."
    kill $HARNESS_PID 2>/dev/null
    while kill -0 $HARNESS_PID 2>/dev/null; do
        sleep 2
    done
    echo "Harness stopped."
else
    echo "No harness running."
fi

echo "Shutdown complete."
