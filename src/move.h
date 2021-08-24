/* Noah Himed
*
* Define and implement the Move type, which stores information necessary to
* update the board representation from a piece movement.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#ifndef OMEGAZERO_SRC_MOVE_H
#define OMEGAZERO_SRC_MOVE_H

#include "constants.h"

struct Move {
  bool is_ep = false;
  int captured_piece = kNA;
  // Indicate if the move is not a castling move, queenside castling, or
  // kingside castling.
  int castling_type = kNA;
  int end_sq = kNA;
  int moving_piece = kNA;
  int moving_player = kNA;
  // Indicate a new en passent target square when a double pawn push
  // is made by either player.
  int new_ep_target_sq = kNA;
  int promoted_piece = kNA;
  int start_sq = kNA;
};

#endif // OMEGAZERO_SRC_MOVE_H
