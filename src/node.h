/* Noah Himed
 *
 * Define and implement the Node type, which stores information necessary to
 * traversal of a search tree in the Perft function.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_NODE_H
#define OMEGAZERO_SRC_NODE_H

#include "board.h"
#include "move.h"

namespace omegazero {

struct Node {
  Node(const Board& board, S8 depth, S8 player) : game_state(board) {
    curr_depth = depth;
    moving_player = player;
  }

  Board game_state;
  S8 curr_depth;
  S8 moving_player;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_NODE_H
