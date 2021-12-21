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
#include <utility>
#include <vector>

#include "board.h"
#include "move.h"

namespace omegazero {

using std::pair;
using std::unordered_map;
using std::vector;

enum GameStatus : S8 {
  kDraw,
  kOptionalDraw,
  kPlayerCheckmated,
  kPlayerInCheck,
  kPlayerToMove,
};

constexpr S8 kMaxKillerMovesPerPly = 2;

constexpr int kSearchDepth = 5;
// Store values used for the MVV-LVA heuristic. Piece order in array is pawn,
// knight, bishop, rook, queen, king.
constexpr int kVictimWeights[kNumPieceTypes] = {100, 200, 300, 400, 500, 600};
constexpr int kAggressorWeights[kNumPieceTypes] = {-10, -20, -30,
                                                   -40, -50, -60};

class Engine {
 public:
  Engine(Board* board, S8 player_side);

  // Search possible games in a search tree to find the best legal move.
  auto GetBestMove() -> Move;

  auto GetGameStatus() -> S8;
  auto GetUserSide() const -> S8;

  // Count the number of leaves of the tree of specified depth whose root
  // node is is the current board state.
  auto Perft(int depth) -> U64;

  // Find all pseudo-legal moves able to be played at the current board state.
  auto GenerateMoves() const -> vector<Move>;

 private:
  // Compute best evaluation resulting from a legal move for the moving
  // player.
  auto GetBestEval(int depth, int alpha, int beta) -> int;

  // Attempt to predict which moves are likely to be better, and order those
  // towards the front of the move_list to increase the number of moves that
  // can be pruned during alpha-beta pruning.
  auto GetOrderedMoves(vector<Move> move_list) const -> vector<Move>;

  auto AddCastlingMoves(vector<Move>& move_list) const -> void;
  auto AddEpMoves(vector<Move>& move_list, S8 moving_player,
                  S8 other_player) const -> void;
  auto AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                        S8 enemy_player, S8 moving_player, S8 moving_piece,
                        S8 start_sq) const -> void;

  // Define a data structure to hold information about a position in the search
  // tree in the tranposition table.
  struct PosInfo {
    int pos_depth;
    int pos_score;
  };

  Board* board_;

  S8 user_side_;

  // Keep track of information for positions that've already been evaluated.
  unordered_map<U64, PosInfo> transposition_table_;
  // Keep track of the number of times positions have occured during a game.
  unordered_map<U64, S8> pos_rep_table_;
};

inline auto Engine::GetUserSide() const -> S8 { return user_side_; }

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
