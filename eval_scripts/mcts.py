"""Noah Himed

Define a type to perform a Monte Carlo Tree Search (MCTS).

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import chess
import game

from collections import OrderedDict
from math import sqrt


def get_board_hash(s: chess.Board):
    return chess.polygloat.zobrist_hash(s)


class MCTS:
    def __init__(self, c_puct):
        self.c_puct = c_puct
        # Store the Q, N, and P tables as dictionaries associating board states
        # to dictionaries associating actions to values.
        self.Q = OrderedDict()
        self.N = OrderedDict()
        self.P = OrderedDict()
        self.visited_nodes = set()

    def get_policy(self, s: chess.Board):
        """Computes the policy vector pi and its corresponding actions."""

        N_sum = self.__get_node_count(s)
        s_hash = get_board_hash(s)
        pi = [p / N_sum for p in self.P[s_hash].values()]
        actions = self.P[s_hash].keys()

        return actions, pi

    def search(self, s: chess.Board, model):
        """Performs a Monte Carlo Tree Search on the Chess game tree."""

        if game.ended(s):
            return game.get_reward(s)

        s_hash = get_board_hash(s)

        if s not in self.visited_nodes:
            self.visited_nodes.add(s)
            actions = game.generate_moves(s)
            # Compute the policy vector and board evaluation given a list of
            # actions to be taken.
            policy, v = model.predict(s, actions)
            self.P[s_hash] = OrderedDict(zip(actions, policy))
            self.Q[s_hash] = OrderedDict.fromkeys(actions, 0)
            self.N[s_hash] = OrderedDict.fromkeys(actions, 0)
            # Return the value of the current board from the perspective of
            # the other player.
            return -v

        # Perform the "Selection" step in MCTS. Select an optimal child,
        # best_a.
        max_U = -float("inf")
        best_a = -1
        actions = game.generate_moves(s)
        for a in actions:
            # Compute the upper confidence bound on Q-values, U.
            N_sum = self.__get_node_count(s)
            U = (self.Q[s_hash][a] + self.c_puct * self.P[s_hash][a]
                 * sqrt(N_sum) / (1 + self.N[s_hash][a]))
            if U > max_U:
                max_U = U
                best_a = a

        # Perform the "Simulation" step in MCTS. Run a simulated game on the
        # chosen optimal child move, best_a.
        a = best_a
        next_state = game.get_next_state(s, a)
        v = self.search(next_state, model)

        # Update the expected reward and number of times a was taken from s.
        self.Q[s_hash][a] = ((self.N[s_hash][a] * self.Q[s_hash][a] + v)
                             / (self.N[s_hash][a] + 1))
        self.N[s_hash][a] += 1

        return -v

    def __get_node_count(self, s: chess.Board):
        """Compute the number of times s has been visited during a search."""

        s_hash = get_board_hash(s)
        return sum(self.N[s_hash].values())
