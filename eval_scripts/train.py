"""Noah Himed

Perform self-play to train the evaluation function.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import chess
import game
import numpy as np

from mcts import MCTS
from model import Model


def __assign_rewards(training_examples, reward):
    for example in training_examples:
        example.append(reward)


def __self_play(model, c_puct, num_sims):
    training_examples = []
    mcts = MCTS(c_puct)
    s = chess.Board()

    while True:
        # Perform a series of Monte Carlo Tree Searches.
        for _ in np.arange(num_sims):
            mcts.search(s, game, model)

        pi = mcts.get_policy(s)
        training_examples.append([s, pi])
        # Selet a random action a, where a is an index denoting a chess move.
        a = np.random.choice(game.NUM_ACTIONS, p=pi)
        s = game.get_next_state(s, a)
        if game.ended(s):
            __assign_rewards(training_examples, game.get_reward(s))
            return training_examples


def gen_model(num_iters, num_sims, num_eps, c_puct, threshold):
    model = Model()
    training_examples = []
    for _ in np.arange(num_iters):
        for _ in np.arange(num_eps):
            training_examples += __self_play(model, c_puct, num_sims)
        new_model = model.train(training_examples)
        frac_win = game.pit(new_model, model)
        if frac_win > threshold:
            model = new_model

    model.save()


if __name__ == "__main__":
    """Train a model over 80 iterations of 100 episodes of self-play each, with
    25 MCTS simulations per episode. Use a value of 1 for c_puct and a
    model comparision threshold of 55%."""
    gen_model(80, 25, 100, 1, 0.55)
