"""Noah Himed

Define a type to perform a Monte Carlo Tree Search (MCTS).

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

from math import sqrt


class MCTS:
    def __init__(self, c_puct):
        self.c_puct = c_puct
        # Store the Q, N, and P tables as dictionaries associating board states
        # to dictionaries associating actions to values.
        self.Q = {}
        self.N = {}
        self.P = {}
        self.visited_nodes = set()

    def get_policy(self, s):
        """Computes the policy vector pi and its corresponding actions."""

        N_sum = self.__get_node_count(s)
        pi = [p / N_sum for p in self.P[s].values()]
        actions = self.P[s].keys()

        return actions, pi

    def search(self, s, game, model):
        """Performs a Monte Carlo Tree Search on the Chess game tree."""

        if game.ended(s):
            return game.get_reward(s)

        if s not in self.visited_nodes:
            self.visited_nodes.add(s)
            actions = game.generate_moves(s)
            # Compute the policy vector and board evaluation given a list of
            # actions to be taken.
            policy, v = model.predict(s, actions)
            self.P[s] = dict(zip(actions, policy))
            self.Q[s] = dict.fromkeys(actions, 0)
            self.N[s] = dict.fromkeys(actions, 0)
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
            U = (self.Q[s][a] + self.c_puct * self.P[s][a]
                 * sqrt(N_sum) / (1 + self.N[s][a]))
            if U > max_U:
                max_U = U
                best_a = a

        # Perform the "Simulation" step in MCTS. Run a simulated game on the
        # chosen optimal child move, best_a.
        a = best_a
        next_state = game.get_next_state(s, a)
        v = self.search(next_state, game, model)

        # Update the expected reward and number of times a was taken from s.
        self.Q[s][a] = (self.N[s][a] * self.Q[s][a] + v) / (self.N[s][a] + 1)
        self.N[s][a] += 1

        return -v

    def __get_node_count(self, s):
        """Compute the number of times s has been visited during a search."""

        return sum(self.N[s].values())
