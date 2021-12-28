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

#include <queue>
#include <vector>

#include "board.h"
#include "move.h"
#include "transp_table.h"

namespace omegazero {

using std::queue;
using std::unordered_map;
using std::vector;

enum GameStatus : S8 {
  kPlayerToMove,
  kPlayerInCheck,
  kDraw,
  kPlayerCheckmated,
};

constexpr int kSearchDepth = 7;
// Store values used for the MVV-LVA heuristic. Piece order in array is pawn,
// knight, bishop, rook, queen, king.
constexpr int kAggressorSortVals[kNumPieceTypes] = {-1, -2, -3, -4, -5, -6};
constexpr int kVictimSortVals[kNumPieceTypes] = {10, 20, 30, 40, 50, 60};
// Store values used for transposition table move ordering.
constexpr int kTranspTableSortVal = 100;
constexpr int kBestEval = INT32_MAX;
constexpr int kNeutralEval = 0;
// Use -INT32_MAX rather than INT32_MIN to avoid integer overflow when
// multipying by -1 during the search function.
constexpr int kWorstEval = -INT32_MAX;

constexpr S8 kSixPlys = 6;

class Engine {
 public:
  Engine(Board* board, S8 player_side);

  // Search possible games in a search tree to find the best legal move. Act as
  // the root function to call the NegaMax search algorithm.
  auto GetBestMove() -> Move;

  // Check for draws, checks, and checkmates. Note that this function does not
  // check for move repititions.
  auto GetGameStatus() -> S8;
  auto GetUserSide() const -> S8;

  // Count the number of leaves of the tree of specified depth whose root
  // node is is the current board state.
  auto Perft(int depth) -> U64;

  // Find all pseudo-legal moves able to be played at the current board state.
  auto GenerateMoves(bool captures_only = false) const -> vector<Move>;

  // Add a board repitition to keep enforce move repitition rules and return the
  // number of times the current board state has been encountered.
  auto AddBoardRep() -> void;

 private:
  auto RepDetected() const -> bool;

  // Compute best evaluation resulting from a legal move for the moving
  // player by searching the tree of possible moves using the NegaMax algorithm.
  auto Search(Move& best_move, int alpha, int beta, int depth) -> int;
  auto Search(int alpha, int beta, int depth) -> int;
  // Search until a "quiescent" position is reached (no capturing moves can be
  // made) to mitigate the horizon effect.
  auto QuiescenceSearch(int alpha, int beta) -> int;

  // Attempt to predict which moves are likely to be better, and order those
  // towards the front of the move_list to increase the number of moves that
  // can be pruned during alpha-beta pruning.
  auto OrderMoves(vector<Move> move_list, int depth) const -> vector<Move>;
  auto OrderMoves(vector<Move> move_list) const -> vector<Move>;

  auto AddCastlingMoves(vector<Move>& move_list) const -> void;
  auto AddEpMoves(vector<Move>& move_list, S8 moving_player,
                  S8 other_player) const -> void;
  auto AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                        S8 enemy_player, S8 moving_player, S8 moving_piece,
                        S8 start_sq) const -> void;

  Board* board_;

  S8 user_side_;

  queue<U64> pos_rep_table_;

  // Keep track of information for positions that've already been evaluated.
  TranspTable transp_table_;
};

// Implement inline member functions.

inline auto Engine::GetBestMove() -> Move {
  Move best_move;
  Search(best_move, kWorstEval, kBestEval, kSearchDepth);
  return best_move;
}

inline auto Engine::GetUserSide() const -> S8 { return user_side_; }

inline auto Engine::RepDetected() const -> bool {
  // Keep track of the last six plys as an efficient approximation to check for
  // board repititions.
  return pos_rep_table_.size() == kSixPlys &&
         pos_rep_table_.front() == pos_rep_table_.back();
}

inline auto Engine::Search(int alpha, int beta, int depth) -> int {
  Move throwaway_move;
  return Search(throwaway_move, alpha, beta, depth);
}

inline auto Engine::AddBoardRep() -> void {
  U64 board_hash = board_->GetBoardHash();
  pos_rep_table_.push(board_hash);
  // Track the last six positions of the game.
  if (pos_rep_table_.size() == kSixPlys) {
    pos_rep_table_.pop();
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
