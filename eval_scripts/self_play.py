"""Noah Himed

Perform self-play to train the evaluation function.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import numpy as np

from mcts import MCTS


def assign_rewards(training_examples, reward):
    for example in training_examples:
        example.append(reward)


def get_training_examples(model, c_puct, num_sims):
    training_examples = []
    mcts = MCTS(c_puct)
    # TODO: Write the game class.
    game = None
    s = game.state()

    while True:
        # Perform a series of Monte Carlo Tree Searches.
        for _ in range(num_sims):
            mcts.search(s, game, model)

        actions, pi = mcts.get_policy(s)
        training_examples.append([s, pi])
        # Selet a random action a, where a is a UCI string of a chess move.
        a = np.random.choice(actions, p=pi)
        s = game.get_next_state(s, a)
        if game.ended(s):
            assign_rewards(training_examples, game.get_reward(s))
            return training_examples
