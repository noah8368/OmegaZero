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
  bool capture_indicated;
  bool is_ep;
  // Indicate if the move is not a castling move, queenside castling, or
  // kingside castling.
  int castling_type;
  int dest;
  int origin;
  int promoted_piece;
};

#endif // OMEGAZERO_SRC_MOVE_H
