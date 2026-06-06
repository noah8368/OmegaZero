#!/usr/bin/env python3
"""
Perft (performance test) runner for OmegaZero.

Verifies move generator correctness by running perft node counts across
six standard positions from the Chess Programming Wiki and comparing
against known-correct values.

    https://www.chessprogramming.org/Perft_Results

Requires the main engine binary (build/OmegaZero). Build with 'make'.

Subcommands:
    run   — Run perft tests up to a given depth and report pass/fail.
    list  — Show all positions and their expected node counts.

Parameters (run subcommand):
    --max-depth   Maximum depth to test (default: 5). Higher depths take
                  exponentially longer. Depth 5 covers millions of nodes
                  and catches virtually all move generation bugs.
    --engine      Path to OmegaZero binary (default: build/OmegaZero).
    --positions   Comma-separated position numbers to test, e.g. "1,2,5".
                  Default: all six positions.

Output:
    For each position and depth, prints the expected and actual node count
    with a PASS/FAIL verdict. Exits with code 0 if all tests pass, 1 if
    any fail.

Usage:
    python3 scripts/perft.py run                    # all positions, depth 1-5
    python3 scripts/perft.py run --max-depth 6      # deeper (slower)
    python3 scripts/perft.py run --positions 1,2    # only positions 1 and 2
    python3 scripts/perft.py list                   # show all expected values
"""

import argparse
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
DEFAULT_ENGINE = REPO_ROOT / "build" / "OmegaZero"

POSITIONS = [
    {
        "name": "Initial Position",
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "nodes": {
            1: 20,
            2: 400,
            3: 8902,
            4: 197281,
            5: 4865609,
            6: 119060324,
        },
    },
    {
        "name": "Kiwipete",
        "fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
        "nodes": {
            1: 48,
            2: 2039,
            3: 97862,
            4: 4085603,
            5: 193690690,
        },
    },
    {
        "name": "Position 3",
        "fen": "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "nodes": {
            1: 14,
            2: 191,
            3: 2812,
            4: 43238,
            5: 674624,
            6: 11030083,
        },
    },
    {
        "name": "Position 4",
        "fen": "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "nodes": {
            1: 6,
            2: 264,
            3: 9467,
            4: 422333,
            5: 15833292,
        },
    },
    {
        "name": "Position 5",
        "fen": "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "nodes": {
            1: 44,
            2: 1486,
            3: 62379,
            4: 2103487,
            5: 89941194,
        },
    },
    {
        "name": "Position 6",
        "fen": "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "nodes": {
            1: 46,
            2: 2079,
            3: 89890,
            4: 3894594,
            5: 164075551,
        },
    },
]


def run_perft(engine, fen, depth):
    """Run the engine's perft and return the total node count."""
    cmd = [str(engine), "--hce", "-i", fen, "-d", str(depth)]
    proc = subprocess.run(
        cmd, input="q\n", capture_output=True, text=True, timeout=600,
    )

    for line in proc.stdout.splitlines():
        if line.startswith("Total:"):
            return int(line.split(":")[1].strip())

    return None


def cmd_run(args):
    engine = Path(args.engine)
    if not engine.is_absolute():
        engine = REPO_ROOT / engine
    engine = engine.resolve()
    if not engine.exists():
        sys.exit(f"Engine not found: {engine}\nRun 'make' first.")

    if args.positions:
        indices = [int(p) - 1 for p in args.positions.split(",")]
        positions = [POSITIONS[i] for i in indices if 0 <= i < len(POSITIONS)]
    else:
        positions = POSITIONS

    passed = 0
    failed = 0

    print(f"\nOmegaZero Perft Test Suite")
    print(f"Max depth: {args.max_depth}")
    print(f"{'=' * 64}\n")

    for pos in positions:
        name = pos["name"]
        fen = pos["fen"]
        print(f"  {name}")
        print(f"  FEN: {fen}")

        for depth in sorted(pos["nodes"].keys()):
            if depth > args.max_depth:
                break

            expected = pos["nodes"][depth]
            try:
                actual = run_perft(engine, fen, depth)
            except subprocess.TimeoutExpired:
                print(f"    depth {depth}: TIMEOUT (>600s)")
                failed += 1
                continue

            if actual is None:
                print(f"    depth {depth}: ERROR (no output)")
                failed += 1
                continue

            ok = actual == expected
            status = "PASS" if ok else "FAIL"
            if ok:
                passed += 1
            else:
                failed += 1

            print(f"    depth {depth}: {actual:>15,}  expected {expected:>15,}  "
                  f"[{status}]")

        print()

    total = passed + failed
    print(f"{'=' * 64}")
    print(f"  {total} tests: {passed} passed, {failed} failed")
    print(f"{'=' * 64}")

    return 0 if failed == 0 else 1


def cmd_list(_args):
    print(f"\nPerft Test Positions")
    print(f"{'=' * 64}\n")

    for i, pos in enumerate(POSITIONS, 1):
        print(f"  [{i}] {pos['name']}")
        print(f"      FEN: {pos['fen']}")
        for depth, nodes in sorted(pos["nodes"].items()):
            print(f"      depth {depth}: {nodes:>15,}")
        print()


def main():
    parser = argparse.ArgumentParser(
        description="OmegaZero perft test suite")
    sub = parser.add_subparsers(dest="command")

    run_p = sub.add_parser("run", help="Run perft tests")
    run_p.add_argument("--max-depth", type=int, default=5,
                       help="Maximum depth to test (default: 5)")
    run_p.add_argument("--engine", default="build/OmegaZero",
                       help="Path to engine binary (default: build/OmegaZero)")
    run_p.add_argument("--positions", default=None,
                       help="Comma-separated position numbers, e.g. '1,2,5'")

    sub.add_parser("list", help="Show all positions and expected values")

    args = parser.parse_args()

    if args.command is None:
        parser.print_help()
        sys.exit(1)

    if args.command == "run":
        sys.exit(cmd_run(args))
    elif args.command == "list":
        cmd_list(args)


if __name__ == "__main__":
    main()
