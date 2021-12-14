/* Noah Himed
 *
 * Implement the Engine type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "engine.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "move.h"

namespace omegazero {

using std::vector;

// Implement public member functions.

Engine::Engine(Board* board) { board_ = board; }

auto Engine::Perft(int depth) -> U64 {
  // Add to the node count if maximum depth is reached.
  if (depth == 0) {
    return 1ULL;
  }

  // Traverse a game tree of chess positions recursively to count leaf nodes.
  U64 node_count = 0;
  vector<Move> move_list = GenerateMoves();
  for (Move& move : move_list) {
    try {
      board_->MakeMove(move);
      node_count += Perft(depth - 1);
      board_->UnmakeMove(move);
      // Change back to the original player.
      board_->SwitchPlayer();
    } catch (BadMove& e) {
      // Ignore all moves that put the player's king in check.
      continue;
    }
  }
  return node_count;
}

auto Engine::GenerateMoves() const -> std::vector<Move> {
  std::vector<Move> move_list;
  return move_list;
}

}  // namespace omegazero
