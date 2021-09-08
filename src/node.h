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

struct Node {
  Node (const Board& board, const int& depth, const int& player)
    : game_state(board) {
      curr_depth = depth;
      moving_player = player;
  }
  
  Board game_state;
  int curr_depth;
  int moving_player;
};

#endif // OMEGAZERO_SRC_NODE_H
