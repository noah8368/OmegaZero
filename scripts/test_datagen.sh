#!/usr/bin/env bash
# Integration test for the datagen watchdog.
# Tests: clean completion, .exit_status file, SIGABRT crash, SIGTERM shutdown,
#        and watchdog restart behavior.

cd "$(dirname "$0")/.."

export WATCHDOG_POLL_INTERVAL=2

TEST_DIR="$(mktemp -d)/watchdog_test"
mkdir -p "$TEST_DIR"

CONFIG="nnue/config.json"
BACKUP="$TEST_DIR/config_backup.json"
cp "$CONFIG" "$BACKUP"

pass=0
fail=0
total=0

assert_eq() {
    local label="$1" expected="$2" actual="$3"
    total=$((total + 1))
    if [[ "$expected" == "$actual" ]]; then
        echo "  PASS: $label (got $actual)"
        pass=$((pass + 1))
    else
        echo "  FAIL: $label (expected $expected, got $actual)"
        fail=$((fail + 1))
    fi
}

assert_file_exists() {
    local label="$1" path="$2"
    total=$((total + 1))
    if [[ -f "$path" ]]; then
        echo "  PASS: $label"
        pass=$((pass + 1))
    else
        echo "  FAIL: $label ($path not found)"
        fail=$((fail + 1))
    fi
}

assert_file_not_exists() {
    local label="$1" path="$2"
    total=$((total + 1))
    if [[ ! -f "$path" ]]; then
        echo "  PASS: $label"
        pass=$((pass + 1))
    else
        echo "  FAIL: $label ($path exists but shouldn't)"
        fail=$((fail + 1))
    fi
}

assert_file_contains() {
    local label="$1" path="$2" pattern="$3"
    total=$((total + 1))
    if [[ -f "$path" ]] && grep -q "$pattern" "$path" 2>/dev/null; then
        echo "  PASS: $label"
        pass=$((pass + 1))
    else
        echo "  FAIL: $label (pattern '$pattern' not in $path)"
        fail=$((fail + 1))
    fi
}

cleanup() {
    cp "$BACKUP" "$CONFIG"
    pkill -f "datagen_harness" 2>/dev/null || true
    pkill -f "run_datagen" 2>/dev/null || true
    sleep 1
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

write_test_config() {
    local games="$1"
    cat > "$CONFIG" << EOF
{
  "games": $games,
  "st": 0.1,
  "workers": 1,
  "output": "$TEST_DIR/data",
  "val_fraction": 0.0,
  "email": "",
  "name": "test",
  "sync_dest": ""
}
EOF
}

get_latest_run() {
    ls -dt "$TEST_DIR/data"/*/ 2>/dev/null | head -1
}

# ============================================================
echo "=== Test 1: Clean completion ==="
# ============================================================
write_test_config 4
rm -rf "$TEST_DIR/data"
mkdir -p "$TEST_DIR/data"

exit_code=0
./build/datagen_harness > "$TEST_DIR/test1.log" 2>&1 || exit_code=$?

assert_eq "exit code is 0" "0" "$exit_code"

run_dir=$(get_latest_run)
assert_file_exists "training_data.txt created" "${run_dir}training_data.txt"
assert_file_exists "metadata.txt created" "${run_dir}metadata.txt"
assert_file_not_exists "no crash log on clean run" "${run_dir}crash_log.txt"
assert_file_exists ".exit_status written" "${run_dir}.exit_status"
status=$(head -1 "${run_dir}.exit_status" 2>/dev/null || echo "")
assert_eq ".exit_status says 'complete'" "complete" "$status"

echo ""

# ============================================================
echo "=== Test 2: SIGTERM — graceful shutdown ==="
# ============================================================
write_test_config 1000
rm -rf "$TEST_DIR/data"
mkdir -p "$TEST_DIR/data"

./build/datagen_harness > "$TEST_DIR/test2.log" 2>&1 &
DATAGEN_PID=$!

sleep 2
if kill -0 "$DATAGEN_PID" 2>/dev/null; then
    kill -TERM "$DATAGEN_PID" 2>/dev/null || true
    for i in $(seq 1 15); do
        kill -0 "$DATAGEN_PID" 2>/dev/null || break
        sleep 1
    done
    wait "$DATAGEN_PID" 2>/dev/null || true

    run_dir=$(get_latest_run)
    if [[ -n "$run_dir" ]]; then
        assert_file_exists "metadata written on SIGTERM" "${run_dir}metadata.txt"
        assert_file_exists "training data saved on SIGTERM" "${run_dir}training_data.txt"
        assert_file_exists ".exit_status written on SIGTERM" "${run_dir}.exit_status"
        status=$(head -1 "${run_dir}.exit_status" 2>/dev/null || echo "")
        assert_eq ".exit_status says 'shutdown'" "shutdown" "$status"
    else
        echo "  FAIL: no run directory found after SIGTERM"
        total=$((total + 4)); fail=$((fail + 4))
    fi
else
    echo "  SKIP: Process exited before SIGTERM could be sent"
    total=$((total + 4)); fail=$((fail + 4))
fi

echo ""

# ============================================================
echo "=== Test 3: SIGABRT — no .exit_status file ==="
# ============================================================
write_test_config 1000
rm -rf "$TEST_DIR/data"
mkdir -p "$TEST_DIR/data"

./build/datagen_harness > "$TEST_DIR/test3.log" 2>&1 &
DATAGEN_PID=$!

sleep 2
if kill -0 "$DATAGEN_PID" 2>/dev/null; then
    kill -ABRT "$DATAGEN_PID" 2>/dev/null || true
    sleep 1
    wait "$DATAGEN_PID" 2>/dev/null || true

    run_dir=$(get_latest_run)
    if [[ -n "$run_dir" ]]; then
        assert_file_not_exists "no .exit_status on SIGABRT" "${run_dir}.exit_status"
    else
        echo "  FAIL: no run directory found"
        total=$((total + 1)); fail=$((fail + 1))
    fi
else
    echo "  SKIP: Process exited before SIGABRT"
    total=$((total + 1)); fail=$((fail + 1))
fi

echo ""

# ============================================================
echo "=== Test 4: Watchdog — clean completion ==="
# ============================================================
write_test_config 4
rm -rf "$TEST_DIR/data"
mkdir -p "$TEST_DIR/data"

./scripts/run_datagen.sh > "$TEST_DIR/test4_watchdog.log" 2>&1 &
WATCHDOG_PID=$!

for i in $(seq 1 60); do
    kill -0 "$WATCHDOG_PID" 2>/dev/null || break
    sleep 1
done

assert_file_contains "watchdog logged clean completion" \
    "$TEST_DIR/test4_watchdog.log" "Clean completion"
assert_file_contains "watchdog exited cleanly" \
    "$TEST_DIR/test4_watchdog.log" "Watchdog exiting"

# .exit_status should be consumed (deleted) by watchdog
run_dir=$(get_latest_run)
if [[ -n "$run_dir" ]]; then
    assert_file_not_exists ".exit_status consumed by watchdog" "${run_dir}.exit_status"
fi

echo ""

# ============================================================
echo "=== Test 5: Watchdog — detects SIGABRT, writes crash log, restarts ==="
# ============================================================
write_test_config 200
rm -rf "$TEST_DIR/data"
mkdir -p "$TEST_DIR/data"

./scripts/run_datagen.sh > "$TEST_DIR/test5_watchdog.log" 2>&1 &
WATCHDOG_PID=$!

# Wait for datagen to start and create output dir
sleep 4

# Find the datagen PID started by the watchdog
DATAGEN_PID=$(pgrep -P "$WATCHDOG_PID" -f datagen_harness 2>/dev/null | head -1 || true)
if [[ -z "$DATAGEN_PID" ]]; then
    DATAGEN_PID=$(pgrep -n -f datagen_harness 2>/dev/null || true)
fi

if [[ -n "$DATAGEN_PID" ]] && kill -0 "$DATAGEN_PID" 2>/dev/null; then
    kill -ABRT "$DATAGEN_PID" 2>/dev/null || true
    echo "  Killed datagen PID $DATAGEN_PID with SIGABRT"

    # Wait for watchdog to detect, write crash log, and restart
    sleep 10

    total=$((total + 1))
    if kill -0 "$WATCHDOG_PID" 2>/dev/null; then
        echo "  PASS: Watchdog still running after crash"
        pass=$((pass + 1))
    else
        echo "  FAIL: Watchdog died after crash"
        fail=$((fail + 1))
        echo "  --- watchdog log ---"
        cat "$TEST_DIR/test5_watchdog.log"
        echo "  --- end log ---"
    fi

    assert_file_contains "watchdog logged process crash" \
        "$TEST_DIR/test5_watchdog.log" "Process crash"
    assert_file_contains "watchdog restarted datagen" \
        "$TEST_DIR/test5_watchdog.log" "Starting datagen"

    # Check crash log was written
    crash_log=$(find "$TEST_DIR/data" -name "crash_log.txt" 2>/dev/null | head -1)
    total=$((total + 1))
    if [[ -n "$crash_log" ]]; then
        echo "  PASS: crash_log.txt found"
        pass=$((pass + 1))
        assert_file_contains "crash log has process_crash entry" \
            "$crash_log" "process_crash"
    else
        echo "  FAIL: no crash_log.txt found"
        fail=$((fail + 1))
    fi

    # Config should have remaining games (may equal original if no metadata
    # was written before SIGABRT — that's correct behavior)
    remaining=$(python3 -c "import json; print(json.load(open('$CONFIG'))['games'])")
    total=$((total + 1))
    if [[ "$remaining" -gt 0 && "$remaining" -le 200 ]]; then
        echo "  PASS: config has valid remaining games ($remaining)"
        pass=$((pass + 1))
    else
        echo "  FAIL: config games invalid (got $remaining, expected 1-200)"
        fail=$((fail + 1))
    fi
else
    echo "  SKIP: Could not find datagen PID"
    total=$((total + 6)); fail=$((fail + 6))
fi

# Clean up
kill "$WATCHDOG_PID" 2>/dev/null || true
pkill -f "datagen_harness" 2>/dev/null || true
sleep 2

echo ""

# ============================================================
echo "=== Test 6: Watchdog — SIGTERM passthrough, no restart ==="
# ============================================================
write_test_config 1000
rm -rf "$TEST_DIR/data"
mkdir -p "$TEST_DIR/data"

./scripts/run_datagen.sh > "$TEST_DIR/test6_watchdog.log" 2>&1 &
WATCHDOG_PID=$!

sleep 4

# Find datagen PID
DATAGEN_PID=$(pgrep -P "$WATCHDOG_PID" -f datagen_harness 2>/dev/null | head -1 || true)
if [[ -z "$DATAGEN_PID" ]]; then
    DATAGEN_PID=$(pgrep -n -f datagen_harness 2>/dev/null || true)
fi

if [[ -n "$DATAGEN_PID" ]] && kill -0 "$DATAGEN_PID" 2>/dev/null; then
    # SIGTERM the datagen process directly
    kill -TERM "$DATAGEN_PID" 2>/dev/null || true
    echo "  Sent SIGTERM to datagen PID $DATAGEN_PID"

    # Wait for datagen shutdown + watchdog detection (up to 20s)
    for i in $(seq 1 20); do
        kill -0 "$WATCHDOG_PID" 2>/dev/null || break
        sleep 1
    done

    # Watchdog should have exited (shutdown = no restart)
    total=$((total + 1))
    if ! kill -0 "$WATCHDOG_PID" 2>/dev/null; then
        echo "  PASS: Watchdog exited after SIGTERM shutdown"
        pass=$((pass + 1))
    else
        echo "  FAIL: Watchdog should have exited on SIGTERM shutdown"
        fail=$((fail + 1))
        echo "  --- watchdog log ---"
        cat "$TEST_DIR/test6_watchdog.log" 2>/dev/null || true
        echo "  --- end log ---"
        kill "$WATCHDOG_PID" 2>/dev/null || true
    fi

    assert_file_contains "watchdog logged intentional stop" \
        "$TEST_DIR/test6_watchdog.log" "Intentional stop"
else
    echo "  SKIP: Could not find datagen PID"
    total=$((total + 2)); fail=$((fail + 2))
fi

pkill -f "datagen_harness" 2>/dev/null || true
sleep 1

echo ""

# ============================================================
echo ""
echo "=========================================="
echo "Results: $pass/$total passed, $fail failed"
echo "=========================================="

if [[ "$fail" -gt 0 ]]; then
    exit 1
fi
