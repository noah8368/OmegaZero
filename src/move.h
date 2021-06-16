// Noah Himed
// 20 March 2021
//
// Define and implement the Move type, which stores information necessary to
// update the board representation from a piece movement.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#ifndef OMEGAZERO_SRC_MOVE_H
#define OMEGAZERO_SRC_MOVE_H

#include "constants.h"

struct Move {
  bool is_ep;
  int captured_piece;
  // Indicate if the move is not a castling move, queenside castling, or
  // kingside castling.
  int castling_type;
  int end_sq;
  int moving_piece;
  int moving_player;
  int promoted_piece;
  int start_sq;
};

#endif // OMEGAZERO_SRC_MOVE_H
