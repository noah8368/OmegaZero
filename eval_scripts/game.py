"""Noah Himed

Define an API to manipulate chess board positions.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import chess


def generate_moves(s: chess.Board):
    """Returns a list of strings of moves in Standard Algebraic Notation."""

    san_move_strings = [chess.board.san(move) for move in s.legal_moves]
    return san_move_strings


def get_reward(s: chess.Board):
    """Returns the game reward needed in MCTS."""

    if not s.is_checkmate:
        raise ValueError("position s isn't an ended game")

    # Return a positive reward only is White wins, since the model will
    # always play White.
    if s.outcome() == chess.WHITE:
        return 1
    elif s.outcome() == chess.BLACK:
        return -1
    else:
        return 0


def get_next_state(s: chess.Board, san_move: str):
    """Plays the move denoted by the string formatted in SAN on the board s."""
    s.push_san(san_move)
    if not s.is_valid():
        s.pop()
        raise ValueError("'a' is an illegal move")
    return s


def ended(s: chess.Board):
    return s.is_checkmate() or s.is_stalemate
