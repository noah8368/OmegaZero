"""Noah Himed

Define neural network model architectures to evaluate board states.

Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
"""

from keras.layers import BatchNormalization, Conv2D, Dense, Dropout
from keras.layers import Activation, Flatten, Input, Reshape
from keras.optimizers import Adam


class Model:
    def __init__(self):
        """Defines the model architecture."""
        # game params
        self.num_files = 8
        self.num_ranks = 8
        # TODO: Implement AlphaZero move indexing.
        self.args = args

        # Neural Net
        self.input_boards = Input(shape=(self.num_files, self.num_ranks))    # s: batch_size x num_files x num_ranks

        x_image = Reshape((self.num_files, self.num_ranks, 1))(self.input_boards)                # batch_size  x num_files x num_ranks x 1
        h_conv1 = Activation('relu')(BatchNormalization(axis=3)(Conv2D(args.num_channels, 3, padding='same', use_bias=False)(x_image)))         # batch_size  x num_files x num_ranks x num_channels
        h_conv2 = Activation('relu')(BatchNormalization(axis=3)(Conv2D(args.num_channels, 3, padding='same', use_bias=False)(h_conv1)))         # batch_size  x num_files x num_ranks x num_channels
        h_conv3 = Activation('relu')(BatchNormalization(axis=3)(Conv2D(args.num_channels, 3, padding='valid', use_bias=False)(h_conv2)))        # batch_size  x (num_files-2) x (num_ranks-2) x num_channels
        h_conv4 = Activation('relu')(BatchNormalization(axis=3)(Conv2D(args.num_channels, 3, padding='valid', use_bias=False)(h_conv3)))        # batch_size  x (num_files-4) x (num_ranks-4) x num_channels
        h_conv4_flat = Flatten()(h_conv4)       
        s_fc1 = Dropout(args.dropout)(Activation('relu')(BatchNormalization(axis=1)(Dense(1024, use_bias=False)(h_conv4_flat))))  # batch_size x 1024
        s_fc2 = Dropout(args.dropout)(Activation('relu')(BatchNormalization(axis=1)(Dense(512, use_bias=False)(s_fc1))))          # batch_size x 1024
        self.pi = Dense(self.action_size, activation='softmax', name='pi')(s_fc2)   # batch_size x self.action_size
        self.v = Dense(1, activation='tanh', name='v')(s_fc2)                    # batch_size x 1

        self.model = Model(inputs=self.input_boards, outputs=[self.pi, self.v])
        self.model.compile(loss=['categorical_crossentropy','mean_squared_error'], optimizer=Adam(args.lr))
