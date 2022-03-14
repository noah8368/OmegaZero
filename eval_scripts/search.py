"""Noah Himed

Implement routines to perform a MCTS through AlphaZero's self-play algorithm.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import chess
import game
import numpy as np

from chess.polyglot import zobrist_hash
from enum import IntEnum
from math import sqrt


# Define constants for MCTS.
_DRAW_REWARD = 0
_WIN_REWARD = 1
_LOSS_REWARD = -1
_NUM_ACTIONS = 4672

# Define the number of time steps _T, the number of feature maps per time
# step _M, and the number of additional time-independent layers _L for a game
# state. Set _T to 1 for ease of implementation in the main engine.
_T = 1
_M = 14
_L = 7


def self_play(model, c_puct, num_sims):
    if (num_sims < 2):
        raise ValueError("num_sims must be at least 2")

    training_examples = []
    mcts = __MCTS(c_puct)
    position, s = init_game_state()

    while True:
        # Perform the "Simulation" step of MCTS.
        search_position = position.copy()
        for _ in np.arange(num_sims):
            # Perform a number of MCTS simulations.
            mcts.search(search_position, s, model)

        # Perform the "Expansion" step in MCTS. Choose a single node to expand
        # the search tree by using the policy vector pi from the model, a.
        # Here, a is an index denoting a chess move.
        pi = mcts.get_policy(position)
        # Store the player to move in the last entry of each training example, 
        # with the intent of overwriting this information with the game reward
        # at the end of a game.
        training_examples.append([s, pi, position.turn])
        a = np.random.choice(_NUM_ACTIONS, p=pi)
        update_game_state(position, s, a)
        if game.ended(position):
            # Perform the "Backpropagation" step in MreturnCTS by assigning a
            # "reward" to each training example.
            __assign_rewards(training_examples, position.outcome().winner)
            return training_examples


def init_game_state():
    """Returns the starting position and game state for a chess game."""

    position = chess.Board()

    game_state = np.zeros((game.NUM_RANK, game.NUM_FILE, _M * _T + _L))
    start_time_idx = _M * (_T - 1)
    game_state[:, :, start_time_idx:start_time_idx + _M] = (
        __get_time_plane(position)
    )
    curr_info_idx = _M * _T
    game_state[:, :, curr_info_idx: curr_info_idx + _L] = (
        __get_constant_planes(position)
    )

    return position, game_state


def update_game_state(position: chess.Board, game_state: np.array, a: int):
    """Updates the game state and chess position in place with the action."""

    # Advance all time planes forward by one time step.
    for time_idx in np.arange(_T - 1):
        game_state[:, :, _M * time_idx:_M * (time_idx + 1)] = (
            game_state[:, :, _M * (time_idx + 1):_M * (time_idx + 2)]
        )

    # Play the move denoted by the index a on the current position.
    move = game.get_move(position, a)
    position.push(move)
    if not position.is_legal:
        raise ValueError("Played move is illegal")

    # Update the latest time plane and constant planes in the game state.
    latest_time_idx = _M * (_T - 1)
    game_state[:, :, latest_time_idx:latest_time_idx + _M] = (
        __get_time_plane(position)
    )
    curr_info_idx = _M * _T
    game_state[:, :, curr_info_idx: curr_info_idx + _L] = (
        __get_constant_planes(position)
    )


def __assign_rewards(training_examples, winner: chess.Color):
    """Assigns rewards to a list of training examples."""

    for example in training_examples:
        if winner:
            model_player = example[-1]
            example[-1] = (_WIN_REWARD if model_player == winner
                           else _LOSS_REWARD)
        else:
            example[-1] = _DRAW_REWARD


def __get_time_plane(board: chess.Board):
    """Computes the time plane in a game state for a given chess position"""

    def get_piece_idx(piece: chess.PieceType, player: chess.Color):
        """Convert a piece type and color into a feature map index."""

        player_idx = 0 if player == board.turn else 1
        piece_idx = piece - chess.PAWN
        return game.NUM_PIECES * player_idx + piece_idx

    time_plane = np.zeros((game.NUM_RANK, game.NUM_FILE, _M))
    pieces = [chess.PAWN, chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN,
              chess.KING]
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
                        time_plane[rank, file, piece_idx] = 1
                        break

                    black_piece_on_sq = (
                        board.piece_at(sq).piece_type == piece
                        and board.piece_at(sq).color == chess.BLACK
                    )
                    if black_piece_on_sq:
                        piece_idx = get_piece_idx(piece, chess.BLACK)
                        time_plane[rank, file, piece_idx] = 1
                        break

    # Flip the board representation to the correct orientation.
    PIECE_OFFSET = game.NUM_PIECES * game.NUM_PLAYER
    if board.turn == chess.WHITE:
        time_plane[:PIECE_OFFSET] = np.flipud(time_plane[:PIECE_OFFSET])

    # Fill in repetition information.
    FIRST_REP_IDX = 0
    SECOND_REP_IDX = 1
    if board.is_repetition(1):
        time_plane[:, :, PIECE_OFFSET + FIRST_REP_IDX] = np.full(
            (game.NUM_RANK, game.NUM_FILE), 1
        )
    elif board.is_repetition(2):
        time_plane[:, :, PIECE_OFFSET + SECOND_REP_IDX] = np.full(
            (game.NUM_RANK, game.NUM_FILE), 1
        )

    return time_plane


def __get_constant_planes(board: chess.Board):
    """Computes the constant planes for a chess position in a game state."""

    class LayerIdx(IntEnum):
        """Enumerates the order of the additional L layers in model input."""

        COLOR, FULL_COUNT, HALF_COUNT = np.arange(3)
        W_QS_CASTLE, W_KS_CASTLE, B_QS_CASTLE, B_KS_CASTLE = np.arange(3, 7)

    constant_planes = np.zeros((game.NUM_RANK, game.NUM_FILE, _L))
    # Indicate the side the AI is playing.
    constant_planes[:, :, LayerIdx.COLOR] = np.full(
        (game.NUM_RANK, game.NUM_FILE), 1 - board.turn
    )

    # Set the half and fullmove counter information.
    constant_planes[:, :, LayerIdx.FULL_COUNT] = np.full(
        (game.NUM_RANK, game.NUM_FILE), board.fullmove_number
    )
    constant_planes[:, :, LayerIdx.HALF_COUNT] = np.full(
        (game.NUM_RANK, game.NUM_FILE), board.halfmove_clock
    )

    # Set the castling rights information.
    castling_rights = board.castling_rights
    constant_planes[:, :, LayerIdx.W_QS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_A1))
    )
    constant_planes[:, :, LayerIdx.W_KS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_H1))
    )
    constant_planes[:, :, LayerIdx.B_QS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_A8))
    )
    constant_planes[:, :, LayerIdx.B_KS_CASTLE] = np.full(
        (game.NUM_RANK, game.NUM_FILE),
        int(bool(castling_rights & chess.BB_H8))
    )

    return constant_planes


class __MCTS:
    """Performs, store information about an ongoing Monte Carlo Tree Search."""

    def __init__(self, c_puct):
        self.c_puct = c_puct
        # Store the Q, N, and P tables as dictionaries associating board states
        # to dictionaries associating actions to values.
        self.Q = {}
        self.N = {}
        self.P = {}
        self.visited_nodes = set()

    def get_policy(self, position: chess.Board):
        """Computes the policy vector pi, normalized for legal moves only."""

        position_hash = zobrist_hash(position)
        N_sum = self.__get_node_count(position_hash)
        pi = self.P[position_hash] / N_sum

        # Zero out all illegal moves in the policy vector and re-normalize.
        legal_moves = game.generate_moves(position)
        legal_idx_list = np.in1d(np.arange(_NUM_ACTIONS), legal_moves)
        illegal_idx_list = np.invert(legal_idx_list)
        pi[illegal_idx_list] = 0
        pi /= np.linalg.norm(pi, ord=1)

        return pi

    def search(self, position: chess.Board, s: np.array, model):
        """Performs a Monte Carlo Tree Search on the Chess game tree."""

        if game.ended(position):
            winner = position.outcome().winner
            if winner:
                reward = (_WIN_REWARD if winner == position.turn else
                          _LOSS_REWARD)
                return -reward
            return _DRAW_REWARD

        position_hash = zobrist_hash(position)

        if position_hash not in self.visited_nodes:
            self.visited_nodes.add(position_hash)
            # Compute the policy vector and board evaluation given a list of
            # actions to be taken.
            self.P[position_hash], v = model.predict(s)
            self.Q[position_hash] = np.zeros(_NUM_ACTIONS)
            self.N[position_hash] = np.zeros(_NUM_ACTIONS)
            # Propagate the value of the node estimated by the model up the
            # search path.
            return -v

        # Perform the "Selection" step in MCTS. Select an optimal child,
        # best_a.
        max_U = -float("inf")
        best_a = -1
        actions = game.generate_moves(position)
        for a in actions:
            # Compute the upper confidence bound on Q-values, U.
            N_sum = self.__get_node_count(position_hash)
            U = (self.Q[position_hash][a]
                 + self.c_puct * self.P[position_hash][a] * sqrt(N_sum)
                 / (1 + self.N[position_hash][a]))
            if U > max_U:
                max_U = U
                best_a = a

        a = best_a
        update_game_state(position, s, a)
        v = self.search(position, s, model)

        # Update the expected reward and number of times a was taken from s.
        self.Q[position_hash][a] = ((self.N[position_hash][a] *
                                     self.Q[position_hash][a] + v)
                                    / (self.N[position_hash][a] + 1))
        self.N[position_hash][a] += 1

        return -v

    def __get_node_count(self, position_hash):
        """Compute the number of times s has been visited during a search."""

        return np.sum(self.N[position_hash])
