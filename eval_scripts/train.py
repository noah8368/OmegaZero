"""Noah Himed

Perform self-play to train the evaluation function.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import numpy as np

from architecture import Model
from game import pit
from search import self_play


def train_model(num_iters, num_sims, num_eps, c_puct, threshold):
    model = Model()
    training_examples = []
    for _ in np.arange(num_iters):
        for _ in np.arange(num_eps):
            training_examples += self_play(model, c_puct, num_sims)
        new_model = model.train(training_examples)
        frac_win = pit(new_model, model)
        if frac_win > threshold:
            model = new_model

    model.save()


if __name__ == "__main__":
    """Train a model over 80 iterations of 100 episodes of self-play each, with
    25 MCTS simulations per episode. Use a value of 1 for c_puct and a
    model comparision threshold of 55%."""
    train_model(80, 25, 100, 1, 0.55)
