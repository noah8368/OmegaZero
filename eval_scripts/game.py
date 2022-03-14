"""Noah Himed

Define an API to manipulate chess board positions and index chess moves.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import chess
import random
import numpy as np

from enum import IntEnum
from search import init_game_state, update_game_state


NUM_RANK = 8
NUM_FILE = 8
NUM_SQ = 64
NUM_PLAYER = 2
NUM_PIECES = 6


def pit(new_model, baseline_model, num_games=100):
    """Compares the performance of two models by returning a win ratio."""

    num_new_model_wins = 0
    for _ in np.arange(num_games):
        game = chess.Board()
        game, s = init_game_state()
        new_model_player = chess.WHITE if random.randint(0, 1) else chess.BLACK
        while not ended(game):
            if game.turn == new_model_player:
                move_idx = new_model.get_move(game, s)
            else:
                move_idx = baseline_model.get_move(game, s)
            update_game_state(game, s, move_idx)

        if game.outcome().winner == new_model_player:
            num_new_model_wins += 1

    return num_new_model_wins / num_games


def generate_moves(s: chess.Board):
    """Returns a list of legal moves denoted by their indices."""

    move_idx_list = [get_move_idx(move) for move in s.legal_moves]
    return move_idx_list


def ended(s: chess.Board):
    return s.outcome() is not None


def get_file(sq):
    """Computes the index of the file given a LERF indexed square."""

    return int(sq) & 7


def get_rank(sq):
    """Computes the index of the rank given a LERF indexed square."""

    return int(sq) >> 3


def get_sq(rank, file):
    """Computes the LERF square index given the rank and file indices."""

    return NUM_FILE * rank + file


# Define a set of constants needed to convert between move indices and
# chess.Move objects.
__MAX_NUM_SQ_MOVED = 7
__KNIGHT_MOVE_OFFSET = 56
__UNDERPROMOTION_OFFSET = 64
__NUM_PLANES = 73
__NUM_UNDERPROMOTION_TYPES = 3


class __RayDir(IntEnum):
    """Defines ray directions."""

    NW, N, NE, E, SE, S, SW, W = np.arange(8)


class __KnightDir(IntEnum):
    """Defines knight directions."""

    NNE, NEE, SEE, SSE, SSW, SWW, NWW, NNW = np.arange(8)


def get_move_idx(move: chess.Move):
    """Given a chess.Move object, compute the move index."""

    def get_move_dir(start_sq, target_sq):
        """Computes the type and direction of a move."""

        sq_diff = target_sq - start_sq
        if sq_diff == 0:
            return None

        start_rank = get_rank(start_sq)
        start_file = get_file(start_sq)
        target_rank = get_rank(target_sq)
        target_file = get_file(target_sq)
        # Check if the two squares are on the same diagonal.
        if (start_rank - start_file == target_rank - target_file or
           start_rank + start_file == target_rank + target_file):
            # Check all possible ray directions.
            if sq_diff % 7 == 0:
                return __RayDir.NW if sq_diff > 0 else __RayDir.SE
            if sq_diff % 9 == 0:
                return __RayDir.NE if sq_diff > 0 else __RayDir.SW

        # Check the N, S, E, and W directions.
        if sq_diff % 8 == 0:
            return __RayDir.N if sq_diff > 0 else __RayDir.S
        if start_rank == target_rank:
            return __RayDir.E if sq_diff > 0 else __RayDir.W

        # Check all possible knight directions.
        if sq_diff % 17 == 0:
            return __KnightDir.NNE if sq_diff > 0 else __KnightDir.SSW
        if sq_diff % 15 == 0:
            return __KnightDir.NNW if sq_diff > 0 else __KnightDir.SSE
        if sq_diff % 10 == 0:
            return __KnightDir.NEE if sq_diff > 0 else __KnightDir.SWW
        if sq_diff % 6 == 0:
            return __KnightDir.NWW if sq_diff > 0 else __KnightDir.SEE

        raise ValueError("Unable to compute move direction")

    dir = get_move_dir(move.from_square, move.to_square)
    if type(dir) is __RayDir:
        # Handle all underpromotions, assuming the moving piece is a pawn.
        if move.promotion and move.promotion != chess.QUEEN:
            if dir == __RayDir.NE or dir == __RayDir.SW:
                dir_idx = 2
            elif dir == __RayDir.N or dir == __RayDir.S:
                dir_idx = 1
            elif dir == __RayDir.NW or dir == __RayDir.SE:
                dir_idx = 0
            else:
                raise ValueError("Invalid move passed in")

            underpromotion_idx = move.promotion - chess.KNIGHT
            plane_idx = (__UNDERPROMOTION_OFFSET
                         + dir_idx * __NUM_UNDERPROMOTION_TYPES
                         + underpromotion_idx)
            move_idx = plane_idx * NUM_SQ + move.from_square
            return move_idx

        # Handle all promotions to queen and other ray moves.
        start_rank = get_rank(move.from_square)
        target_rank = get_rank(move.to_square)
        if start_rank != target_rank:
            num_sq_moved = abs(target_rank - start_rank)
        else:
            start_file = get_file(move.from_square)
            target_file = get_file(move.to_square)
            num_sq_moved = abs(target_file - start_file)
        plane_idx = dir * __MAX_NUM_SQ_MOVED + (num_sq_moved - 1)
        move_idx = plane_idx * NUM_SQ + move.from_square
        return move_idx

    if type(dir) is __KnightDir:
        plane_idx = __KNIGHT_MOVE_OFFSET + dir
        move_idx = plane_idx * NUM_SQ + move.from_square
        return move_idx

    raise ValueError("Unable to map move to index")


def get_move(s: chess.Board, move_idx: int):
    """Given a move index, constuct a chess.Move object."""

    move = chess.Move(0, 0)
    move.from_square = move_idx % NUM_SQ
    plane_idx = move_idx // NUM_SQ

    # Define the single step increments per ray direction on a LERF indexed
    # 8x8 board representation for NW, N, NE, E, SE, S, SW, and W.
    RAY_COMPASS_ROSE = [7, 8, 9, 1, -7, -8, -9, -1]

    # Construct a move object for moves along rays that aren't underpromotions.
    if 0 <= plane_idx < __KNIGHT_MOVE_OFFSET:
        dir_idx = plane_idx // __MAX_NUM_SQ_MOVED
        num_sq_moved = plane_idx % __MAX_NUM_SQ_MOVED + 1
        step_inc = RAY_COMPASS_ROSE[dir_idx]
        move.to_square = move.from_square + num_sq_moved * step_inc

        # Check for promotions.
        RANK8 = 7
        RANK1 = 0
        if ((get_rank(move.to_square) == RANK8
           or get_rank(move.to_square) == RANK1)
           and s.piece_at(move.from_square).piece_type == chess.PAWN):
            move.promotion = chess.QUEEN
            return move
        return move

    # Construct a move object for all pawn underpromotions.
    if __UNDERPROMOTION_OFFSET <= plane_idx < __NUM_PLANES:
        dir_idx = ((plane_idx - __UNDERPROMOTION_OFFSET)
                   // __NUM_UNDERPROMOTION_TYPES)
        RANK7 = 6
        start_rank = get_rank(move.from_square)
        if dir_idx == 2:
            dir = __RayDir.NE if (start_rank == RANK7) else __RayDir.SW
        elif dir_idx == 1:
            dir = __RayDir.N if (start_rank == RANK7) else __RayDir.S
        elif dir_idx == 0:
            dir = __RayDir.NW if (start_rank == RANK7) else __RayDir.SE

        step_inc = RAY_COMPASS_ROSE[dir]
        move.to_square = move.from_square + step_inc

        # Determine the piece to promote to.
        promotion_idx = ((plane_idx - __UNDERPROMOTION_OFFSET)
                         % __NUM_UNDERPROMOTION_TYPES)
        move.promotion = chess.KNIGHT + promotion_idx
        return move

    # Define the single step increments per knight move direction on a LERF
    # indexed 8x8 board representation for NNE, NEE, SEE, SSE, SSW, SWW, NWW,
    # NNW.
    KNIGHT_COMPASS_ROSE = [17, 10, -6, -15, -17, -10, 6, 15]

    # Construct a move object for all knight moves.
    if __KNIGHT_MOVE_OFFSET <= plane_idx < __UNDERPROMOTION_OFFSET:
        dir = plane_idx - __KNIGHT_MOVE_OFFSET
        step_inc = KNIGHT_COMPASS_ROSE[dir]
        move.to_square = move.from_square + step_inc
        return move

    raise ValueError("Invalid move index passed in")
