"""Noah Himed

Define neural network model architectures to evaluate board states.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

import chess
import numpy as np
import tensorflow as tf

from game import generate_moves
from tensorflow.keras.layers import BatchNormalization, Conv2D, Dense, Dropout
from tensorflow.keras.layers import Add, MaxPool2D, Activation, Flatten, Input


# Define the number of time steps _T, the number of feature maps per time
# step _M, and the number of additional time-independent layers _L for a game
# state. Set _T to 1 for ease of implementation in the main engine.
_T = 1
_M = 14
_L = 7
_NUM_RANK = 8
_NUM_FILE = 8
_GAME_STATE_DIMS = (_NUM_RANK, _NUM_FILE, _M * _T + _L)
_NUM_ACTIONS = 4672

_BATCH_SIZE = 32


def def_model_A():
    """Defines a 'small' model consisting of a CNN with two Conv2D layers."""

    input = Input(shape=_GAME_STATE_DIMS)

    model = Conv2D(filters=25, kernel_size=3, padding="same")(input)
    model = BatchNormalization(axis=3)(model)
    model = Activation("relu")(model)
    model = MaxPool2D(pool_size=2, strides=1, padding="valid")(model)

    model = Conv2D(filters=50, kernel_size=3, padding="same")(model)
    model = BatchNormalization(axis=3)(model)
    model = Activation("relu")(model)
    model = MaxPool2D(pool_size=2, strides=1, padding="valid")(model)

    model = Flatten()(model)

    model = Dense(units=512)(model)
    model = BatchNormalization(axis=1)(model)
    model = Activation("relu")(model)
    output = Dropout(rate=0.6)(model)

    pi = Dense(_NUM_ACTIONS, activation="softmax", name="pi")(output)
    v = Dense(1, activation="tanh", name='v')(output)
    model = tf.keras.Model(inputs=input, outputs=[pi, v], name='A')
    return model


def def_model_B():
    """Defines a 'medium' model consisting of a CNN with four Conv2D layers."""

    input = Input(shape=_GAME_STATE_DIMS)

    model = Conv2D(filters=25, kernel_size=3, padding="same")(input)
    model = BatchNormalization(axis=3)(model)
    model = Activation("relu")(model)
    model = MaxPool2D(pool_size=2, strides=1, padding="valid")(model)

    model = Conv2D(filters=50, kernel_size=3, padding="same")(model)
    model = BatchNormalization(axis=3)(model)
    model = Activation("relu")(model)
    model = MaxPool2D(pool_size=2, strides=1, padding="valid")(model)

    model = Conv2D(filters=100, kernel_size=3, padding="same")(model)
    model = BatchNormalization(axis=3)(model)
    model = Activation("relu")(model)
    model = MaxPool2D(pool_size=2, strides=1, padding="valid")(model)

    model = Conv2D(filters=200, kernel_size=3, padding="same")(model)
    model = BatchNormalization(axis=3)(model)
    model = Activation("relu")(model)
    model = MaxPool2D(pool_size=2, strides=1, padding="valid")(model)

    model = Flatten()(model)

    model = Dense(units=1024)(model)
    model = BatchNormalization(axis=1)(model)
    model = Activation("relu")(model)
    output = Dropout(rate=0.6)(model)

    pi = Dense(_NUM_ACTIONS, activation="softmax", name="pi")(output)
    v = Dense(1, activation="tanh", name='v')(output)
    model = tf.keras.Model(inputs=input, outputs=[pi, v], name='B')
    return model


def def_model_C():
    """Defines a 'large' ResNet model with eight Conv2D layers."""

    def __residual_block(input, downsample, filters, kernel_size):
        """Defines a residual block in a ResNet"""

        block = Conv2D(kernel_size=kernel_size,
                       strides=(1 if not downsample else 2),
                       filters=filters,
                       padding="same")(input)
        block = BatchNormalization(axis=3)(block)
        block = Activation("relu")(block)
        block = Conv2D(kernel_size=kernel_size,
                       strides=1,
                       filters=filters,
                       padding="same")(block)

        if downsample:
            input = Conv2D(kernel_size=1,
                           strides=2,
                           filters=filters,
                           padding="same")(input)
        sum = Add()([input, block])
        model = BatchNormalization(axis=3)(sum)
        output = Activation("relu")(model)
        return output

    input = Input(shape=_GAME_STATE_DIMS)
    __NUM_FILTERS = 64

    model = Conv2D(filters=__NUM_FILTERS, kernel_size=3, padding="same")(input)
    model = BatchNormalization(axis=3)(model)
    model = Activation("relu")(model)

    # Define eight Conv2D layers with skip connections between each and three
    # "sections", where the number of filters shrinks within sections and
    # grows between sections.
    sections = [2, 4, 2]
    for section_idx in range(len(sections)):
        num_blocks = sections[section_idx]
        for block_idx in range(num_blocks):
            model = __residual_block(model,
                                     downsample=(block_idx == 0
                                                 and section_idx != 0),
                                     filters=__NUM_FILTERS,
                                     kernel_size=3)
        __NUM_FILTERS *= 2

    model = Flatten()(model)
    model = Dense(2048, activation='softmax')(model)
    model = BatchNormalization(axis=1)(model)
    model = Activation("relu")(model)
    output = Dropout(rate=0.6)(model)

    pi = Dense(_NUM_ACTIONS, activation="softmax", name="pi")(output)
    v = Dense(1, activation="tanh", name='v')(output)
    model = tf.keras.Model(inputs=input, outputs=[pi, v], name='C')
    return model


class EvalModel:
    """"""
    def __init__(self, architecture_choice):
        """Defines the model architecture."""

        self.model_name = architecture_choice

        # Determine the model structure.
        if architecture_choice == 'A':
            self.model = def_model_A()
        elif architecture_choice == 'B':
            self.model = def_model_B()
        elif architecture_choice == 'C':
            self.model = def_model_C()
        else:
            raise ValueError("Unkown model architecture denoted")

        # Display model details to the user.
        self.model.summary()
        self.model.compile(loss=["categorical_crossentropy",
                                 "mean_squared_error"],
                           optimizer="Adam")

    def copy(self):
        """Performs a shallow copy of the current EvalModel object."""

        model_copy = EvalModel(self.model_name)
        model_copy.model = self.model
        return model_copy

    def get_move(self, position: chess.Board(), game_state: np.array):
        pi, _ = self.predict(game_state)

        # Zero out all illegal moves in the policy vector and re-normalize.
        legal_moves = generate_moves(position)
        legal_idx_list = np.in1d(np.arange(_NUM_ACTIONS), legal_moves)
        illegal_idx_list = np.invert(legal_idx_list)
        pi[illegal_idx_list] = 0
        pi /= np.linalg.norm(pi, ord=1)

        return np.argmax(pi)

    def predict(self, game_state: np.array):
        game_state = game_state[np.newaxis, :, :, :]
        pi, v = self.model.predict(game_state)
        return pi[0], v[0]

    def save(self):
        self.model.save(self.model_name + ".h5", include_optimizer=False)

    def train(self, training_examples):
        game_states, policy_vectors, rewards = list(zip(*training_examples))
        game_states = np.asarray(game_states)
        policy_vectors = np.asarray(policy_vectors)
        rewards = np.asarray(rewards)
        self.model.fit(x=game_states, y=[policy_vectors, rewards],
                       batch_size=_BATCH_SIZE)
