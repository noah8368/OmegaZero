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

#include <unordered_map>
#include <vector>

#include "board.h"
#include "move.h"

namespace omegazero {

using std::unordered_map;
using std::vector;

enum GameStatus : S8 {
  kDraw,
  kOptionalDraw,
  kPlayerCheckmated,
  kPlayerInCheck,
  kPlayerToMove,
};

class Engine {
 public:
  Engine(Board* board, S8 player_side);

  // Search possible games in a search tree to find the best move. This is the
  // "search" function. This returns a legal move.
  auto GetBestMove(int depth) -> Move;
  // Calculate the best move, make it on the board, then return it.
  auto TakeTurn(int search_depth) -> Move;

  auto GetGameStatus() -> S8;
  auto GetUserSide() const -> S8;

  // Count the number of leaves of the tree of specified depth whose root
  // node is is the current board state.
  auto Perft(int depth) -> U64;

  // Find all pseudo-legal moves able to be played at the current board state.
  auto GenerateMoves() const -> vector<Move>;

 private:
  // Return the maximum evaluation that results from all possible legal moves.
  auto GetMaxScore(int depth) -> int;
  // Return the minimum evaluation that results from all possible legal moves.
  auto GetMinScore(int depth) -> int;

  auto AddCastlingMoves(vector<Move>& move_list) const -> void;
  auto AddEpMoves(vector<Move>& move_list, S8 moving_player,
                  S8 other_player) const -> void;
  auto AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                        S8 enemy_player, S8 moving_player, S8 moving_piece,
                        S8 start_sq) const -> void;

  Board* board_;

  S8 user_side_;

  // Keep track of the number of times positions have occured during a game.
  unordered_map<U64, S8> pos_rep_table_;
};

inline auto Engine::GetUserSide() const -> S8 { return user_side_; }

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
