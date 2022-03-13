"""Noah Himed

Define an API to manipulate chess board positions.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import enum
from multiprocessing.sharedctypes import Value
from random import randbytes
import chess


NUM_ACTIONS = 4672


def pit(new_model, baseline_model, num_games=100):
    """Compares the performance of two models by returning a win ratio."""

    num_new_model_wins = 0
    for _ in range(num_games):
        s = chess.Board()
        player = new_model
        while not ended(s):
            move_idx = player.get_move(s)
            move_str = __get_move(s, move_idx)
            s.push(move_str)
            player = baseline_model if player == new_model else new_model

        if s.outcome() == chess.WHITE:
            num_new_model_wins += 1

    return num_new_model_wins / num_games


def generate_moves(s: chess.Board):
    """Returns a list of legal moves denoted by their indices."""

    move_idx_list = [__get_move_idx(move) for move in s.legal_moves]
    return move_idx_list


def get_reward(s: chess.Board):
    """Returns the game reward needed in MCTS."""

    if not ended(s):
        raise ValueError("position isn't an ended game")

    # Return a positive reward only is White wins, since the model will
    # always play White.
    if s.outcome() == chess.WHITE:
        return 1
    elif s.outcome() == chess.BLACK:
        return -1
    else:
        return 0


def get_next_state(s: chess.Board, move_idx: int):
    """Plays the move denoted by the move index on the board s."""

    move_str = __get_move(move_idx)
    s.push_uci(move_str)
    if not s.is_valid():
        s.pop()
        raise ValueError("move is illegal")
    return s


def ended(s: chess.Board):
    return s.is_checkmate() or s.is_stalemate


# Define a set of constants needed to convert between move indices and
# chess.Move objects.
__NUM_SQ = 64
__MAX_NUM_SQ_MOVED = 7
__KNIGHT_MOVE_OFFSET = 56
__UNDERPROMOTION_OFFSET = 64
__NUM_PLANES = 73
__NUM_UNDERPROMOTION_TYPES = 3


class __RayDir(enum.Enum):
    """Defines ray directions."""

    NW, N, NE, E, SE, S, SW, W = range(8)


class __KnightDir(enum.Enum):
    """Defines knight directions."""

    NNE, NEE, SEE, SSE, SSW, SWW, NWW, NNW = range(8)


def __get_move_idx(move: chess.Move):
    """Given a chess.Move object, compute the move index."""

    def get_move_dir(start_sq, target_sq):
        """Computes the type and direction of a move."""

        sq_diff = target_sq - start_sq
        if sq_diff == 0:
            return None

        # Check for knight directions.
        if sq_diff % 17 == 0:
            return __KnightDir.NNE if sq_diff > 0 else __KnightDir.SSW
        if sq_diff % 15 == 0:
            return __KnightDir.NNW if sq_diff > 0 else __KnightDir.SSE
        if sq_diff % 10 == 0:
            return __KnightDir.NEE if sq_diff > 0 else __KnightDir.SWW
        if sq_diff % 6 == 0:
            return __KnightDir.NWW if sq_diff > 0 else __KnightDir.SEE

        # Check all possible ray directions.
        if sq_diff % 7 == 0:
            return __RayDir.NW if sq_diff > 0 else __RayDir.SE
        if sq_diff % 8 == 0:
            return __RayDir.N if sq_diff > 0 else __RayDir.S
        if sq_diff % 9 == 0:
            return __RayDir.NE if sq_diff > 0 else __RayDir.SW
        start_rank = __get_rank(start_sq)
        target_rank = __get_rank(target_sq)
        if start_rank == target_rank:
            return __RayDir.E if sq_diff > 0 else __RayDir.W

        return None

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
            move_idx = plane_idx * __NUM_SQ + move.from_square
            return move_idx

        # Handle all promotions to queen and other ray moves.
        start_rank = __get_rank(move.from_square)
        target_rank = __get_rank(move.to_square & 7)
        num_sq_moved = abs(target_rank - start_rank)
        plane_idx = dir * __MAX_NUM_SQ_MOVED + (num_sq_moved - 1)
        move_idx = plane_idx * __NUM_SQ + move.from_square
        return move_idx

    if type(dir) is __KnightDir:
        plane_idx = __KNIGHT_MOVE_OFFSET + dir
        move_idx = plane_idx * __NUM_SQ + move.from_square
        return move_idx

    raise ValueError("Unable to map move to index")


def __get_move(s: chess.Board, move_idx: int):
    """Given a move index, constuct a chess.Move object."""

    move = chess.Move(0, 0)
    move.from_square = move_idx % __NUM_SQ
    plane_idx = move_idx // __NUM_SQ

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
        if ((__get_rank(move.to_square) == RANK8
           or __get_rank(move.to_square) == RANK1)
           and s.piece_at(move.from_square) == chess.PAWN):
            move.promotion = chess.QUEEN
            return move
        return move

    # Construct a move object for all pawn underpromotions.
    if __UNDERPROMOTION_OFFSET <= plane_idx < __NUM_PLANES:
        dir_idx = ((plane_idx - __UNDERPROMOTION_OFFSET)
                   // __NUM_UNDERPROMOTION_TYPES)
        RANK7 = 6
        start_rank = __get_rank(move.from_square)
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

    raise ValueError("Invalid move index passed in")


def __get_rank(sq):
    """Computes the index of the rank given a LERF indexed square."""

    return sq & 7
