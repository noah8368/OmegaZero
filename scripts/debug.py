#!/usr/bin/env python3
"""
Debug runner for OmegaZero.

Runs self-play crash detection: the engine plays against itself and
reports any errors (illegal moves, crashes, empty moves). On failure,
prints the board state and move history, and writes a crash log.

Requires the debug harness binary (build/debug_harness). Build with
'make debug' (includes AddressSanitizer for memory error detection).

Parameters:
    --games        Number of self-play games (default: 10).
    --search-time  Search time per move in seconds (default: 0.1).
    --harness      Path to debug_harness binary (default: build/debug_harness).

Usage:
    python3 scripts/debug.py                        # 10 games, 0.1s/move
    python3 scripts/debug.py --games 100            # longer soak test
    python3 scripts/debug.py --search-time 0.5      # deeper search per move
    python3 scripts/debug.py --games 1000 --search-time 0.05  # fast stress test
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
DEFAULT_HARNESS = REPO_ROOT / "build" / "debug_harness"


def ensure_binary(harness, target):
    if not harness.exists():
        print(f"Binary not found, building with 'make {target}'...")
        result = subprocess.run(
            ["make", target, f"-j{os.cpu_count()}"],
            cwd=REPO_ROOT, capture_output=True, text=True,
        )
        if result.returncode != 0:
            print(result.stderr)
            sys.exit(f"Build failed. Run 'make {target}' manually.")
        if not harness.exists():
            sys.exit(f"Build succeeded but binary not found at {harness}")


def main():
    parser = argparse.ArgumentParser(
        description="OmegaZero self-play crash detection")
    parser.add_argument("--games", type=int, default=10,
                        help="Number of self-play games (default: 10)")
    parser.add_argument("--search-time", type=float, default=0.1,
                        help="Search time per move in seconds (default: 0.1)")
    parser.add_argument("--harness", default="build/debug_harness",
                        help="Path to debug harness (default: build/debug_harness)")

    args = parser.parse_args()

    harness = Path(args.harness)
    if not harness.is_absolute():
        harness = REPO_ROOT / harness
    harness = harness.resolve()
    ensure_binary(harness, "debug")

    cmd = [str(harness), str(args.games), str(args.search_time)]

    print(f"\nOmegaZero Self-Play Debug")
    print(f"Games: {args.games}  Search time: {args.search_time}s")
    print(f"{'=' * 64}\n")

    try:
        result = subprocess.run(cmd, timeout=3600)
        sys.exit(result.returncode)
    except subprocess.TimeoutExpired:
        print("\nTIMEOUT: debug harness exceeded 1 hour")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted.")
        sys.exit(1)


if __name__ == "__main__":
    main()
