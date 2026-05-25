#!/bin/bash
#
# Launch parallel NNUE training data generation across multiple cores.
# Usage: ./scripts/parallel_data_gen.sh [--workers N] [--games G] [--st S]
#
# After all workers finish, the per-worker files are concatenated into
# scripts/nnue_training_data/training_data.txt and the individual files
# are removed.

set -e

WORKERS=8
TOTAL_GAMES=22000
ST=2.0
ENGINE="build/OmegaZero"
OUT_DIR="scripts/nnue_training_data"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workers) WORKERS="$2"; shift 2 ;;
    --games)   TOTAL_GAMES="$2"; shift 2 ;;
    --st)      ST="$2"; shift 2 ;;
    --engine)  ENGINE="$2"; shift 2 ;;
    *)         echo "Unknown option: $1"; exit 1 ;;
  esac
done

GAMES_PER_WORKER=$(( TOTAL_GAMES / WORKERS ))
REMAINDER=$(( TOTAL_GAMES % WORKERS ))

if [ ! -f "$ENGINE" ]; then
  echo "Engine not found at $ENGINE — run 'make' first."
  exit 1
fi

mkdir -p "$OUT_DIR"

echo "Launching $WORKERS workers ($GAMES_PER_WORKER games each, ${ST}s/move)"
echo "Total games: $TOTAL_GAMES"
echo "Output: $OUT_DIR/training_data.txt"
echo ""

PIDS=()

for i in $(seq 1 "$WORKERS"); do
  GAMES="$GAMES_PER_WORKER"
  if [ "$i" -le "$REMAINDER" ]; then
    GAMES=$(( GAMES + 1 ))
  fi

  OUT_FILE="$OUT_DIR/data_worker_${i}.txt"
  echo "  Worker $i: $GAMES games → $OUT_FILE"

  python3 scripts/generate_training_data.py \
    --engine "$ENGINE" \
    --games "$GAMES" \
    --st "$ST" \
    --output "$OUT_FILE" \
    &

  PIDS+=($!)
done

echo ""
echo "All workers launched (PIDs: ${PIDS[*]})"
echo "Waiting for completion... (Ctrl+C to stop — partial data is saved)"

FAILED=0
for pid in "${PIDS[@]}"; do
  if ! wait "$pid"; then
    FAILED=$((FAILED + 1))
  fi
done

if [ "$FAILED" -gt 0 ]; then
  echo ""
  echo "WARNING: $FAILED worker(s) exited with errors."
fi

echo ""
echo "Combining worker output files..."
cat "$OUT_DIR"/data_worker_*.txt > "$OUT_DIR/training_data.txt"
rm -f "$OUT_DIR"/data_worker_*.txt

TOTAL_POS=$(wc -l < "$OUT_DIR/training_data.txt")
echo "Done: $TOTAL_POS positions saved to $OUT_DIR/training_data.txt"
