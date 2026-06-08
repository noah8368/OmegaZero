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
#include <cassert>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

#include "board.h"
#include "move.h"
#include "out_of_time.h"
#include "transposition_table.h"
#ifdef SEARCH_TRACE
#include "search_trace.h"
#endif

namespace omegazero {

using std::begin;
using std::clamp;
using std::copy;
using std::end;
using std::invalid_argument;
using std::numeric_limits;
using std::pair;
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

constexpr int kSearchLimit = 128;

constexpr int kBestEval = INT32_MAX - 1;
constexpr int kNeutralEval = -25;
// Use -INT32_MAX rather than INT32_MIN to avoid integer overflow when
// multipying by -1 during the search function.
constexpr int kWorstEval = 1 - INT32_MAX;
constexpr int kInvalidEval = INT32_MAX;

class Engine {
 public:
  Engine(Board* board, S8 player_side, float search_time);

  // Searches possible games in a search tree to find the best legal move. Act
  // as the root function to call the Negamax search algorithm in an iterative
  // deepening framework.
  auto GetBestMove() -> Move;
  auto GetBestMove(int& node_score) -> Move;

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
  auto ClearHistory() -> void;
  auto SetSearchTime(float t) -> void;

#ifdef BENCHMARK
  auto GetTotalNodes() const -> uint64_t {
    return total_nodes_ + nodes_since_time_check_;
  }
  auto BenchmarkReport(int search_depth) -> void;
#endif

#ifdef SEARCH_TRACE
  auto GetSearchTrace() const -> const SearchTrace& { return search_trace_; }
#endif

 private:
  auto InEndgame() const -> bool;
  auto IsKillerMove(const Move& move, int ply) const -> bool;
  auto RepDetected() const -> bool;
  // Return if Zugzwang is unlikely, indicating Null-Move Heuristic should be
  // used.
  auto ZugzwangUnlikely() const -> bool;

  // Computes best evaluation resulting from a legal move for the moving
  // player by searching the tree of possible moves using the Negamax
  // algorithm.
  auto ValidateTtMove(const Move& move) const -> bool;
  auto AspirationSearch(int prev_score, int depth, int ply, Move& best_move)
      -> int;
  auto Pvs(int alpha, int beta, int depth, int ply, bool null_move_allowed)
      -> int;
  auto Pvs(Move& pv_move, int alpha, int beta, int depth, int ply,
           bool null_move_allowed) -> int;
  // Search until a "quiescent" position is reached (no capturing moves can be
  // made) to mitigate the horizon effect.
  auto QuiescenceSearch(int alpha, int beta, int qs_depth = 20) -> int;

  // Attempts to predict which moves are likely to be better, and order those
  // towards the front of the move_list to increase the number of moves that
  // can be pruned during alpha-beta pruning.
  auto OrderMoves(const vector<Move>& move_list, int ply) const -> vector<Move>;
  auto OrderMoves(const vector<Move>& move_list) const -> vector<Move>;

  auto AddCastlingMoves(vector<Move>& move_list) const -> void;
  auto AddEpMoves(vector<Move>& move_list, S8 moving_player,
                  S8 other_player) const -> void;
  auto AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                        S8 enemy_player, S8 moving_player, S8 moving_piece,
                        S8 start_sq) const -> void;
  auto CheckSearchTime() -> void;
  auto RecordKillerMove(const Move& move, int ply) -> void;
  auto UpdateHistoryHeuristic(const Move& move, int bonus) -> void;

  auto ProbeTt(Move& pv_move, int& alpha, int& beta, int depth, int& result)
      -> bool;
  auto TryNullMovePrune(int alpha, int beta, int depth, int ply,
                        bool at_pv_node, bool in_check) -> bool;
  auto TryReverseFutilityPrune(int static_eval, int depth, int beta,
                               bool at_pv_node, bool in_check) -> bool;
  auto ShouldFutilityPrune(const Move& move, int static_eval, int depth,
                           bool at_pv_node, bool in_check, int alpha) -> bool;
  auto ShouldLateMovePrune(const Move& move, int num_quiet_searched, int depth,
                           bool at_pv_node, bool gives_check, bool in_check,
                           int ply) -> bool;
  auto ShouldSeePrune(const Move& move, int depth, bool at_pv_node,
                      bool gives_check, bool in_check, int see_val) -> bool;
  auto ComputeLmrReduction(int depth, int legal_moves, S8 player_to_move,
                           const Move& move) -> int;
  auto RecordBetaCutoff(const Move& move, int depth, int ply,
                        const vector<Move>& searched_quiet_moves) -> void;
  auto StoreTtEntry(int best_eval, int orig_alpha, int beta, int depth,
                    const Move& best_move) -> void;

#ifdef SEARCH_TRACE
  auto TraceInitSearch() -> void;
  auto TraceStartIteration() -> void;
  auto TraceSaveIteration(int score, int depth, SearchTrace& out) -> void;
  auto TraceRestoreAfterTimeout(const SearchTrace& saved) -> void;
  auto TraceFinishSearch() -> void;
  auto TraceSuppressRecording() -> bool;
  auto TraceResumeRecording(bool was_recording) -> void;
  auto TracePrune(const Move& move, S8 player, const char* reason, int ply)
      -> void;
  auto TraceBeginMove(const Move& move, S8 player, int ply) -> int;
  auto TraceEndMove(int eval, int ply) -> void;
  auto TraceMarkBetaCutoff(const std::vector<Move>& move_list, size_t move_idx,
                           S8 player, int ply) -> void;
  auto TraceMarkPv(int best_trace_idx, int ply) -> void;
  auto TraceIsActive(int ply) const -> bool;
#endif

  // Track if a board evaluation is improving as a line is being seared.
  bool improving_;

  Board* board_;

  float search_time_;

  high_resolution_clock::time_point search_start_;

  int history_heuristic_[kNumPlayers][kNumPieceTypes][kNumSq];
  int nodes_since_time_check_;
  int eval_history_[kSearchLimit];

#ifdef BENCHMARK
  uint64_t total_nodes_;
#endif

  Move countermove_table_[kNumPieceTypes][kNumSq];

  pair<Move, Move> killer_moves_[kSearchLimit];

  vector<U64> pos_history_;

  S8 user_side_;

#ifdef SEARCH_TRACE
  SearchTrace search_trace_;
  vector<int> trace_path_;
#endif

  // Keep track of information for positions that've already been evaluated.
  TranspositionTable transposition_table_;
};

// Implement public inline member functions.

inline auto Engine::GetBestMove() -> Move {
  int node_score;
  return GetBestMove(node_score);
}

inline auto Engine::GetUserSide() const -> S8 { return user_side_; }

inline auto Engine::AddPosToHistory() -> void {
  pos_history_.push_back(board_->GetBoardHash());
}

inline auto Engine::ClearHistory() -> void { pos_history_.clear(); }

inline auto Engine::SetSearchTime(float t) -> void {
  constexpr float kMinSearchTime = 0.01f;
  if (t < kMinSearchTime) t = kMinSearchTime;
  search_time_ = t;
}

// Implement private inline member functions.

inline auto Engine::InEndgame() const -> bool {
  Bitboard white_queens = board_->GetPiecesByType(kQueen, kWhite);
  Bitboard black_queens = board_->GetPiecesByType(kQueen, kBlack);
  bool no_queens = !static_cast<bool>(white_queens | black_queens);
  bool no_rooks = !static_cast<bool>(board_->GetPiecesByType(kRook, kNA));
  Bitboard white_minor_pieces = board_->GetPiecesByType(kKnight, kWhite) |
                                board_->GetPiecesByType(kBishop, kWhite);
  Bitboard black_minor_pieces = board_->GetPiecesByType(kKnight, kBlack) |
                                board_->GetPiecesByType(kBishop, kBlack);
  // Indicate the game has entered the endgame if both players either don't have
  // a queen, or have only one queen in addition to at most one minor piece.
  return (no_queens) | (no_rooks &&
                        (GetNumSetSq(white_queens) <= 1 &&
                         GetNumSetSq(white_minor_pieces) <= 1) &&
                        (GetNumSetSq(black_queens) <= 1 &&
                         GetNumSetSq(black_minor_pieces) <= 1));
}

inline auto Engine::IsKillerMove(const Move& move, int ply) const -> bool {
  if (ply < 0 || ply >= kSearchLimit) return false;

  return killer_moves_[ply].first == move || killer_moves_[ply].second == move;
}

inline auto Engine::RepDetected() const -> bool {
  if (pos_history_.size() < 5) return false;
  U64 current = pos_history_.back();
  // Scan backwards checking every 2nd entry (same side to move).
  for (int pos_idx = static_cast<int>(pos_history_.size()) - 5; pos_idx >= 0;
       pos_idx -= 2) {
    if (pos_history_[pos_idx] == current) return true;
  }
  return false;
}

inline auto Engine::ZugzwangUnlikely() const -> bool {
  S8 player_to_move = board_->GetPlayerToMove();
  Bitboard non_pawn_king_pieces =
      board_->GetPiecesByType(kKnight, player_to_move) |
      board_->GetPiecesByType(kBishop, player_to_move) |
      board_->GetPiecesByType(kRook, player_to_move) |
      board_->GetPiecesByType(kQueen, player_to_move);

  return GetNumSetSq(non_pawn_king_pieces) >= 1;
}

inline auto Engine::Pvs(int alpha, int beta, int depth, int ply,
                        bool null_move_allowed) -> int {
  Move pv_move;
  return Pvs(pv_move, alpha, beta, depth, ply, null_move_allowed);
}

inline auto Engine::CheckSearchTime() -> void {
  if (++nodes_since_time_check_ < 4096) {
    return;
  }

#ifdef BENCHMARK
  total_nodes_ += 4096;
#endif

  nodes_since_time_check_ = 0;
  float time_since_search_started =
      duration_cast<duration<float>>(high_resolution_clock::now() -
                                     search_start_)
          .count();
  if (time_since_search_started >= search_time_) {
    throw OutOfTime();
  }
}

inline auto Engine::RecordKillerMove(const Move& move, int ply) -> void {
  if (ply < 0 || ply >= kSearchLimit) return;
  if (move != killer_moves_[ply].first && move != killer_moves_[ply].second) {
    killer_moves_[ply].second = killer_moves_[ply].first;
    killer_moves_[ply].first = move;
  }
}

constexpr int kMaxHistoryBonus = 16384;

inline auto Engine::UpdateHistoryHeuristic(const Move& move, int bonus)
    -> void {
  S8 player_to_move = board_->GetPlayerToMove();
  bonus = clamp(bonus, -kMaxHistoryBonus, kMaxHistoryBonus);
  int& history =
      history_heuristic_[player_to_move][move.moving_piece][move.target_sq];
  // Update the history heuristic value using the history gravity formula.
  history += (bonus - history * abs(bonus) / kMaxHistoryBonus);
}

constexpr int kFutilityMargin = 200;
constexpr int kMaxFutilityPruningDepth = 2;

inline auto Engine::ShouldFutilityPrune(const Move& move, int static_eval,
                                        int depth, bool at_pv_node,
                                        bool in_check, int alpha) -> bool {
  return depth <= kMaxFutilityPruningDepth && !at_pv_node && !in_check &&
         move.captured_piece == kNA && move.promoted_to_piece == kNA &&
         static_eval + depth * kFutilityMargin <= alpha;
}

constexpr int kMaxLateMovePruningDepth = 2;

inline auto Engine::ShouldLateMovePrune(const Move& move,
                                        int num_quiet_searched, int depth,
                                        bool at_pv_node, bool gives_check,
                                        bool in_check, int ply) -> bool {
  int lmpThreshold = 6 + 2 * depth * depth;
  // Lower the move count threshold when not the line's evaluations aren't
  // improving.
  if (!improving_) {
    lmpThreshold /= 2;
  }
  return !at_pv_node && depth <= kMaxLateMovePruningDepth &&
         num_quiet_searched > lmpThreshold && move.captured_piece == kNA &&
         move.promoted_to_piece == kNA && !gives_check && !in_check &&
         !IsKillerMove(move, ply);
}

constexpr int kMaxSeePruningDepth = 5;

inline auto Engine::ShouldSeePrune(const Move& move, int depth, bool at_pv_node,
                                   bool gives_check, bool in_check, int see_val)
    -> bool {
  if (at_pv_node || depth > kMaxSeePruningDepth || move.captured_piece == kNA ||
      gives_check || in_check) {
    return false;
  }
  return see_val < -depth * 100;
}

inline auto Engine::ValidateTtMove(const Move& move) const -> bool {
  if (move.moving_piece == kNA && move.castling_type == kNA) {
    return false;
  }
  if (move.castling_type != kNA) {
    return true;
  }
  if (!SqOnBoard(move.start_sq)) {
    return false;
  }
  return board_->GetPieceOnSq(move.start_sq) == move.moving_piece &&
         board_->GetPlayerOnSq(move.start_sq) == board_->GetPlayerToMove();
}

inline auto Engine::TryReverseFutilityPrune(int static_eval, int depth,
                                            int beta, bool at_pv_node,
                                            bool in_check) -> bool {
  if (depth > 2 || at_pv_node || in_check) {
    return false;
  }
  if (improving_) {
    // Prune less aggressively when the line's static evaluations are improving.
    return static_eval - (depth - 1) * kFutilityMargin >= beta;
  }
  return static_eval - depth * kFutilityMargin >= beta;
}

constexpr int kHistoryLmrThreshold = -1000;

inline auto Engine::ComputeLmrReduction(int depth, int legal_moves,
                                        S8 player_to_move, const Move& move)
    -> int {
  int reduction = static_cast<int>(sqrt(static_cast<double>(depth - 1)) +
                                   sqrt(static_cast<double>(legal_moves - 1)));
  int history_score =
      history_heuristic_[player_to_move][move.moving_piece][move.target_sq];
  if (history_score > 0) {
    --reduction;
  } else if (history_score < kHistoryLmrThreshold) {
    ++reduction;
  }
  if (!improving_) {
    // Reduce depth more if the line's evaluations aren't improving.
    ++reduction;
  }
  return std::max(1, reduction);
}

inline auto Engine::StoreTtEntry(int best_eval, int orig_alpha, int beta,
                                 int depth, const Move& best_move) -> void {
  if (best_eval <= orig_alpha) {
    transposition_table_.Update(board_, depth, best_eval, kAllNode);
  } else if (best_eval >= beta) {
    transposition_table_.Update(board_, depth, best_eval, kCutNode, best_move);
  } else {
    transposition_table_.Update(board_, depth, best_eval, kPvNode, best_move);
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
