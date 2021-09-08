/* Noah Himed
*
* Define the Engine type. Engine objects contains a pseudo-legal move generator,
* a search tree of possible game states, an evaluation function, and a search
* function.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#ifndef OMEGAZERO_SRC_ENGINE_H_
#define OMEGAZERO_SRC_ENGINE_H_

#include <vector>

#include "board.h"
#include "move.h"

class Engine {
public:
  // Return a vector of pseudo-legal moves.
  std::vector<Move> GenerateMoves(const int& player, const Board& board) const;
private:
};

#endif // OMEGAZERO_SRC_ENGINE_H_
