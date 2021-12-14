/* Noah Himed
 *
 * Define the Engine type. Engine objects contains a pseudo-legal move
 * generator, a search tree of possible game states, an evaluation function, and
 * a search function.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_ENGINE_H_
#define OMEGAZERO_SRC_ENGINE_H_

#include <vector>

#include "board.h"
#include "move.h"

namespace omegazero {

using std::vector;

class Engine {
 public:
  Engine(Board* board);

  // Count the number of leaves of the tree of specified depth whose root
  // node is is the current board state.
  auto Perft(int depth) -> U64;

  // Find all pseudo-legal moves able to be played at the current board state.
  auto GenerateMoves() const -> vector<Move>;

 private:
  Board* board_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
