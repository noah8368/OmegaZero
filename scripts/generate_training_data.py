#!/usr/bin/env python3
"""
NNUE training data generator for OmegaZero.

Plays self-play games via UCI and records (fen, search_score, game_result) tuples
for training an NNUE evaluation network.

Output format (plain text, one line per position):
    <fen> | <score> | <result>

Where:
    fen    = standard FEN string for the position
    score  = engine search score in centipawns from white's perspective
    result = game outcome from white's perspective: 1.0 (white win),
             0.0 (black win), or 0.5 (draw)

Usage:
    python3 scripts/generate_training_data.py --games 1000 --st 0.5
    python3 scripts/generate_training_data.py --games 100 --st 1.0 --output data/training.txt
"""

import argparse
import os
import random
import subprocess
import sys
import time
from pathlib import Path


START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

MAX_MOVES_PER_GAME = 400
SKIP_FIRST_N_PLIES = 8
RANDOM_OPENING_PLIES = 8


class UciEngine:
    def __init__(self, engine_path):
        self.proc = subprocess.Popen(
            [engine_path, "--uci"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._send("uci")
        self._read_until("uciok")
        self._send("isready")
        self._read_until("readyok")

    def _send(self, cmd):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def _read_until(self, token):
        while True:
            line = self.proc.stdout.readline().strip()
            if token in line:
                return line

    def new_game(self):
        self._send("ucinewgame")
        self._send("isready")
        self._read_until("readyok")

    def set_position(self, moves):
        if moves:
            self._send("position startpos moves " + " ".join(moves))
        else:
            self._send("position startpos")

    def go(self, movetime_ms):
        self._send(f"go movetime {movetime_ms}")
        bestmove_line = self._read_until("bestmove")
        parts = bestmove_line.split()
        idx = parts.index("bestmove")
        return parts[idx + 1]

    def quit(self):
        try:
            self._send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def board_from_moves(moves):
    """Reconstruct FEN from a sequence of UCI moves using python-chess."""
    import chess
    board = chess.Board()
    for m in moves:
        board.push_uci(m)
    return board


def get_random_opening_moves(n_plies):
    """Play n random legal moves to diversify openings."""
    import chess
    board = chess.Board()
    moves = []
    for _ in range(n_plies):
        legal = list(board.legal_moves)
        if not legal:
            break
        move = random.choice(legal)
        moves.append(move.uci())
        board.push(move)
    if board.is_game_over():
        return get_random_opening_moves(n_plies)
    return moves


def play_game(engine, movetime_ms, use_random_openings):
    """Play one self-play game. Returns list of (fen, score_white_pov) and result."""
    import chess

    engine.new_game()

    if use_random_openings:
        uci_moves = get_random_opening_moves(RANDOM_OPENING_PLIES)
    else:
        uci_moves = []

    engine.set_position(uci_moves)

    board = chess.Board()
    for m in uci_moves:
        board.push_uci(m)

    positions = []

    for ply in range(len(uci_moves), MAX_MOVES_PER_GAME):
        if board.is_game_over():
            break

        engine.set_position(uci_moves)
        bestmove = engine.go(movetime_ms)

        if bestmove == "0000" or bestmove == "(none)":
            break

        score = board.evaluate() if hasattr(board, "evaluate") else None

        fen = board.fen()
        stm = board.turn
        static_eval = simple_eval(board)

        if ply >= SKIP_FIRST_N_PLIES and not board.is_check():
            positions.append((fen, static_eval))

        try:
            board.push_uci(bestmove)
        except ValueError:
            break
        uci_moves.append(bestmove)

    result = game_result(board)
    return positions, result


def simple_eval(board):
    """Material-only eval in centipawns from white's perspective.

    Used as a lightweight label when the engine's internal eval isn't
    accessible through UCI. The real labels come from blending this with
    the game outcome during training.
    """
    import chess

    piece_values = {
        chess.PAWN: 100,
        chess.KNIGHT: 320,
        chess.BISHOP: 330,
        chess.ROOK: 500,
        chess.QUEEN: 900,
    }
    score = 0
    for pt, val in piece_values.items():
        score += val * (len(board.pieces(pt, chess.WHITE))
                        - len(board.pieces(pt, chess.BLACK)))
    return score


def game_result(board):
    """Return game result from white's perspective: 1.0 / 0.0 / 0.5."""
    import chess
    result = board.result()
    if result == "1-0":
        return 1.0
    elif result == "0-1":
        return 0.0
    elif result == "1/2-1/2":
        return 0.5
    else:
        return 0.5


def play_game_with_eval(engine, movetime_ms, use_random_openings):
    """Play one self-play game capturing search scores via UCI info lines.

    Falls back to material eval if the engine doesn't emit 'info ... score cp'.
    """
    import chess

    engine.new_game()

    if use_random_openings:
        uci_moves = get_random_opening_moves(RANDOM_OPENING_PLIES)
    else:
        uci_moves = []

    board = chess.Board()
    for m in uci_moves:
        board.push_uci(m)

    positions = []

    for ply in range(len(uci_moves), MAX_MOVES_PER_GAME):
        if board.is_game_over():
            break

        engine.set_position(uci_moves)

        bestmove, score_cp = engine_go_with_score(engine, movetime_ms)

        if bestmove == "0000" or bestmove == "(none)":
            break

        fen = board.fen()
        stm_is_white = board.turn == chess.WHITE

        if score_cp is not None:
            score_white = score_cp if stm_is_white else -score_cp
        else:
            score_white = simple_eval(board)

        if ply >= SKIP_FIRST_N_PLIES and not board.is_check():
            positions.append((fen, score_white))

        try:
            board.push_uci(bestmove)
        except ValueError:
            break
        uci_moves.append(bestmove)

    result = game_result(board)
    return positions, result


def engine_go_with_score(engine, movetime_ms):
    """Send 'go movetime' and parse both the last info score and bestmove."""
    engine._send(f"go movetime {movetime_ms}")

    score_cp = None
    while True:
        line = engine.proc.stdout.readline().strip()

        if "score cp" in line:
            parts = line.split()
            try:
                idx = parts.index("cp")
                score_cp = int(parts[idx + 1])
            except (ValueError, IndexError):
                pass
        elif "score mate" in line:
            parts = line.split()
            try:
                idx = parts.index("mate")
                mate_in = int(parts[idx + 1])
                score_cp = 30000 if mate_in > 0 else -30000
            except (ValueError, IndexError):
                pass

        if "bestmove" in line:
            parts = line.split()
            idx = parts.index("bestmove")
            bestmove = parts[idx + 1]
            return bestmove, score_cp


def main():
    parser = argparse.ArgumentParser(
        description="Generate NNUE training data via OmegaZero self-play"
    )
    parser.add_argument(
        "--engine", default="build/OmegaZero",
        help="Path to OmegaZero binary (default: build/OmegaZero)",
    )
    parser.add_argument(
        "--games", type=int, default=100,
        help="Number of self-play games (default: 100)",
    )
    parser.add_argument(
        "--st", type=float, default=0.5,
        help="Think time per move in seconds (default: 0.5)",
    )
    parser.add_argument(
        "--output", default="scripts/nnue_training_data/training_data.txt",
        help="Output file path (default: scripts/nnue_training_data/training_data.txt)",
    )
    parser.add_argument(
        "--no-random-openings", action="store_true",
        help="Disable random opening move diversification",
    )
    parser.add_argument(
        "--append", action="store_true",
        help="Append to existing output file instead of overwriting",
    )
    args = parser.parse_args()

    engine_path = Path(args.engine).resolve()
    if not engine_path.exists():
        sys.exit(f"Engine not found: {engine_path}\nRun 'make' first.")

    try:
        import chess
    except ImportError:
        sys.exit(
            "python-chess is required for FEN generation.\n"
            "Install with: pip3 install python-chess"
        )

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    movetime_ms = int(args.st * 1000)
    use_random = not args.no_random_openings
    mode = "a" if args.append else "w"

    total_positions = 0
    results = {1.0: 0, 0.0: 0, 0.5: 0}

    print(f"Generating training data: {args.games} games, {args.st}s/move")
    print(f"Random openings: {'yes' if use_random else 'no'}")
    print(f"Output: {out_path}")
    print()

    engine = UciEngine(str(engine_path))

    try:
        with open(out_path, mode) as f:
            for game_num in range(1, args.games + 1):
                t0 = time.time()

                positions, result = play_game_with_eval(
                    engine, movetime_ms, use_random
                )

                for fen, score in positions:
                    f.write(f"{fen} | {score} | {result}\n")

                total_positions += len(positions)
                results[result] = results.get(result, 0) + 1

                elapsed = time.time() - t0
                result_str = {1.0: "1-0", 0.0: "0-1", 0.5: "draw"}[result]
                print(
                    f"  Game {game_num:4d}/{args.games}  "
                    f"{result_str:>4s}  "
                    f"{len(positions):3d} positions  "
                    f"{elapsed:.1f}s  "
                    f"(total: {total_positions})",
                    flush=True,
                )

                f.flush()

    except KeyboardInterrupt:
        print("\nInterrupted — partial data saved.")
    finally:
        engine.quit()

    w = results.get(1.0, 0)
    b = results.get(0.0, 0)
    d = results.get(0.5, 0)
    games_played = w + b + d

    print(f"\nDone: {games_played} games, {total_positions} positions saved")
    print(f"Results: W:{w}  D:{d}  L:{b}")
    print(f"Output: {out_path}")


if __name__ == "__main__":
    main()
