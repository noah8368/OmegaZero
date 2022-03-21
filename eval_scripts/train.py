"""Noah Himed

Perform self-play to train the evaluation function.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import numpy as np

from architecture import EvalModel
from game import pit
from search import self_play


def train_model(model_choice, num_iters, num_sims, num_eps, c_puct, threshold):
    model = EvalModel(model_choice)
    training_examples = []
    for iter_idx in np.arange(num_iters):
        print("---Training iteration", str(iter_idx + 1) + "/"
              + str(num_iters) + "---")
        # Collect training data from self-play.
        for ep_idx in np.arange(num_eps):
            print("Self-play episode", str(ep_idx + 1) + '/' + str(num_eps))
            training_examples += self_play(model, c_puct, num_sims)
        new_model = model.copy()
        new_model.train(training_examples)
        frac_win = pit(new_model, model)
        if frac_win > threshold:
            model = new_model
    print("MODEL GENERATED")
    model.save()


if __name__ == "__main__":
    train_model(model_choice='C', num_iters=10, num_sims=10, num_eps=10,
                c_puct=1, threshold=0.55)
