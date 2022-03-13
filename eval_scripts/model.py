"""Noah Himed

Define neural network model architectures to evaluate board states.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import chess
import game
import numpy as np

from enum import IntEnum

'''from keras.layers import BatchNormalization, Conv2D, Dense, Dropout
from keras.layers import Activation, Flatten, Input, Reshape
from keras.optimizers import Adam'''


def get_model_input(state_list):
    """Convert a list of board states into an image stack for model input."""

    class LayerIdx(IntEnum):
        """Enumerates the order of the additional L layers in model input."""

        COLOR, FULL_COUNT, HALF_COUNT = np.arange(3)
        W_QS_CASTLE, W_KS_CASTLE, B_QS_CASTLE, B_KS_CASTLE = np.arange(3, 7)

    def get_piece_idx(piece: chess.PieceType, player: chess.Color):
        """Convert a piece type and color into a feature map index."""

        player_idx = chess.WHITE - player
        piece_idx = piece - chess.PAWN
        return game.NUM_PIECES * player_idx + piece_idx

    # Define the number of time steps T, the number of feature maps per time
    # step M, and the number of additional time-independent layers L.
    T = len(state_list)
    M = 14
    L = 7

    PIECE_OFFSET = 12
    model_input = np.zeros((game.NUM_RANK, game.NUM_FILE, M * T + L))

    pieces = [chess.PAWN, chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN,
              chess.KING]

    # Set all time-dependent feature maps.
    for t in np.arange(T):
        time_offset = M * t
        board = state_list[t]

        # Fill in the piece presence information.
        for rank in np.arange(game.NUM_RANK):
            for file in np.arange(game.NUM_FILE):
                sq = game.get_sq(rank, file)
                if board.piece_at(sq):
                    for piece in pieces:
                        white_piece_on_sq = (
                            board.piece_at(sq).piece_type == piece
                            and board.piece_at(sq).color == chess.WHITE
                        )
                        if white_piece_on_sq:
                            piece_idx = get_piece_idx(piece, chess.WHITE)
                            model_input[rank, file,
                                        time_offset + piece_idx] = 1
                            break

                        black_piece_on_sq = (
                            board.piece_at(sq).piece_type == piece
                            and board.piece_at(sq).color == chess.BLACK
                        )
                        if black_piece_on_sq:
                            piece_idx = get_piece_idx(piece, chess.BLACK)
                            model_input[rank, file,
                                        time_offset + piece_idx] = 1
                            break

        # Fill in threefold repetition information.
        if board.can_claim_threefold_repetition():
            time_offset = M * t
            player_idx = 0 if board.turn == chess.WHITE else 1
            model_input[:, :,
                        time_offset + PIECE_OFFSET + player_idx] = np.full(
                            (game.NUM_RANK, game.NUM_FILE), 1
                        )

    # Set all time-independent feature maps.
    TIME_OFFSET = M * T
    print(TIME_OFFSET)

    # Indicate the side the AI is playing.
    # TODO: Add non-constant player to move.
    model_input[:, :, TIME_OFFSET + LayerIdx.COLOR] = np.full(
        (game.NUM_RANK, game.NUM_FILE), 1 - chess.WHITE
    )

    # Set the half and fullmove counter information.
    last_state = state_list[-1]
    model_input[:, :, TIME_OFFSET + LayerIdx.FULL_COUNT] = np.full(
        (game.NUM_RANK, game.NUM_FILE), last_state.fullmove_number
    )
    model_input[:, :, TIME_OFFSET + LayerIdx.HALF_COUNT] = np.full(
        (game.NUM_RANK, game.NUM_FILE), last_state.halfmove_clock
    )

    # Set the castling rights information.
    castling_rights = last_state.castling_rights
    model_input[:, :, TIME_OFFSET + LayerIdx.W_QS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_A1))
    )
    model_input[:, :, TIME_OFFSET + LayerIdx.W_KS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_H1))
    )
    model_input[:, :, TIME_OFFSET + LayerIdx.B_QS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_A8))
    )
    model_input[:, :, TIME_OFFSET + LayerIdx.B_KS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_H8))
    )

    return model_input


class Model:
    def __init__(self):
        """Defines the model architecture."""
        # TODO: Implement a network.
        pass
