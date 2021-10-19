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

class Engine {
 public:
  // Return a vector of pseudo-legal moves.
  auto GenerateMoves(S8 player, const Board& board) const -> std::vector<Move>;

 private:
  auto AddCastlingMoves(S8 moving_player, const Board& board,
                        std::vector<Move>* move_list) const -> void;
  auto AddEpMoves(S8 moving_player, S8 other_player, const Board& board,
                  std::vector<Move>* move_list) const -> void;
  auto AddMovesForPiece(Bitboard* attack_map, const Board& board,
                        std::vector<Move>* move_list, S8 other_player,
                        S8 start_sq, S8 moving_piece, S8 moving_player) const
      -> void;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
