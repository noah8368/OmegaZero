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

#include <algorithm>
#include <chrono>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

#include "board.h"
#include "move.h"
#include "out_of_time.h"
#include "transp_table.h"

namespace omegazero {

using std::begin;
using std::copy;
using std::end;
using std::invalid_argument;
using std::pair;
using std::queue;
using std::unordered_map;
using std::vector;
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;

enum GameStatus : S8 {
  kPlayerToMove,
  kPlayerInCheck,
  kDraw,
  kPlayerCheckmated,
};

constexpr int kRanOutOfTime = 2;
constexpr int kSearchLimit = 30;

// Store values used for transposition table move ordering.
constexpr int kBestEval = INT32_MAX;
constexpr int kNeutralEval = 0;
// Use -INT32_MAX rather than INT32_MIN to avoid integer overflow when
// multipying by -1 during the search function.
constexpr int kWorstEval = -INT32_MAX;

constexpr S8 kSixPlys = 6;

class Engine {
 public:
  Engine(Board* board, S8 player_side, float search_time);

  // Searches possible games in a search tree to find the best legal move. Act
  // as the root function to call the NegaMax search algorithm in an iterative
  // deepening framework.
  auto GetBestMove() -> Move;

  // Check for draws, checks, and checkmates. Note that this function does not
  // check for move repititions.
  auto GetGameStatus() -> S8;
  auto GetUserSide() const -> S8;

  // Counts the number of leaves of the tree of specified depth whose root
  // node is is the current board state.
  auto Perft(int depth) -> U64;

  // Finds all pseudo-legal moves able to be played at the current board state.
  auto GenerateMoves(bool captures_only = false) const -> vector<Move>;

  // Adds a board repitition to keep enforce move repitition rules and return
  // the number of times the current board state has been encountered.
  auto AddPosToHistory() -> void;

 private:
  auto IsKillerMove(const Move& move, int ply) const -> bool;
  auto RepDetected() const -> bool;

  // Computes best evaluation resulting from a legal move for the moving
  // player by searching the tree of possible moves using the NegaMax algorithm.
  auto Search(int search_depth) -> Move;
  auto Search(int alpha, int beta, int depth, int ply) -> int;
  auto Search(Move& move, int alpha, int beta, int depth, int ply) -> int;
  // Search until a "quiescent" position is reached (no capturing moves can be
  // made) to mitigate the horizon effect.
  auto QuiescenceSearch(int alpha, int beta) -> int;

  // Attempts to predict which moves are likely to be better, and order those
  // towards the front of the move_list to increase the number of moves that
  // can be pruned during alpha-beta pruning.
  auto OrderMoves(vector<Move> move_list, int ply) const -> vector<Move>;
  auto OrderMoves(vector<Move> move_list) const -> vector<Move>;

  auto AddCastlingMoves(vector<Move>& move_list) const -> void;
  auto AddEpMoves(vector<Move>& move_list, S8 moving_player,
                  S8 other_player) const -> void;
  auto AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                        S8 enemy_player, S8 moving_player, S8 moving_piece,
                        S8 start_sq) const -> void;
  auto CheckSearchTime() const -> void;
  auto ClearHistory() -> void;
  auto RecordKillerMove(const Move& move, int ply) -> void;

  Board* board_;

  float search_time_;

  high_resolution_clock::time_point search_start_;

  pair<Move, Move> killer_moves_[kSearchLimit];

  queue<U64> pos_history_;

  S8 user_side_;

  // Keep track of information for positions that've already been evaluated.
  TranspTable transp_table_;
};

// Implement public inline member functions.

inline auto Engine::GetUserSide() const -> S8 { return user_side_; }

// Implement private inline member functions.

inline auto Engine::IsKillerMove(const Move& move, int ply) const -> bool {
  if (ply < 0 || ply >= kSearchLimit) {
    throw invalid_argument("ply in Engine::IsKillerMove()");
  }

  return killer_moves_[ply].first == move || killer_moves_[ply].second == move;
}

inline auto Engine::RepDetected() const -> bool {
  // Keep track of the last six plys as an efficient approximation to check for
  // board repititions.
  return pos_history_.size() == kSixPlys &&
         pos_history_.front() == pos_history_.back();
}

inline auto Engine::Search(int search_depth) -> Move {
  Move best_move;
  Search(best_move, kWorstEval, kBestEval, search_depth, 0);
  return best_move;
}

inline auto Engine::Search(int alpha, int beta, int depth, int ply) -> int {
  Move throwaway_move;
  return Search(throwaway_move, alpha, beta, depth, ply);
}

inline auto Engine::AddPosToHistory() -> void {
  U64 board_hash = board_->GetBoardHash();
  pos_history_.push(board_hash);
  // Track the last six positions of the game.
  while (pos_history_.size() > kSixPlys) {
    pos_history_.pop();
  }
}

inline auto Engine::CheckSearchTime() const -> void {
  float time_since_search_started =
      duration_cast<duration<float>>(high_resolution_clock::now() -
                                     search_start_)
          .count();
  if (time_since_search_started >= search_time_) {
    throw OutOfTime();
  }
}

inline auto Engine::ClearHistory() -> void {
  queue<U64> cleared_history;
  pos_history_.swap(cleared_history);
}

inline auto Engine::RecordKillerMove(const Move& move, int ply) -> void {
  if (move != killer_moves_[ply].first) {
    killer_moves_[ply].second = killer_moves_[ply].first;
    killer_moves_[ply].first = move;
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
