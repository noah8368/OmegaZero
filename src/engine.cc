/* Noah Himed
 *
 * Implement the Engine type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "engine.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "game.h"
#include "move.h"
#include "out_of_time.h"
#include "transposition_table.h"

namespace omegazero {

using std::max;
using std::min;
using std::pair;
using std::runtime_error;
using std::sort;
using std::unordered_map;
using std::vector;
using std::chrono::high_resolution_clock;

// Store values used for the MVV-LVA heuristic. Piece order in array is
// pawn, knight, bishop, rook, queen, king.
constexpr int kAggressorSortVals[kNumPieceTypes] = {-1, -2, -3, -4, -5, -6};
constexpr int kVictimSortVals[kNumPieceTypes] = {10, 20, 30, 40, 50, 60};

constexpr int kHistoryLmrThreshold = -1000;
constexpr S8 kNumEarlyMoves = 3;
constexpr S8 kMinReductionDepth = 3;

// Implement public member functions.

Engine::Engine(Board* board, S8 player_side, float search_time) {
  assert(board != nullptr);
  board_ = board;

  constexpr float kMinSearchTime = 0.1f;
  if (search_time < kMinSearchTime) {
    throw invalid_argument("Search time must be at least 0.1s");
  }
  search_time_ = search_time;

  if (tolower(player_side) == 'w') {
    user_side_ = kWhite;
  } else if (tolower(player_side) == 'b') {
    user_side_ = kBlack;
  } else if (tolower(player_side) == 'r') {
    // Pick a random side for the user to play as.
    srand(static_cast<int>(time(0)));
    user_side_ = static_cast<S8>(rand() % static_cast<int>(kNumPlayers));
  } else {
    throw invalid_argument("invalid side choice");
  }

  // Initialize the history heuristic table to 0.
  memset(history_heuristic_, 0, sizeof(history_heuristic_));
}

auto Engine::GetBestMove() -> Move {
  assert(!pos_history_.empty());
  board_->ClearPawnTable();
  for (auto& km : killer_moves_) km = {};
  // Save game-level history size; OutOfTime can unwind past the per-move
  // restores in NegamaxSearch, leaving stale hashes behind.
  size_t saved_history_size = pos_history_.size();
  Move best_move;
  Move move;
  board_->SavePos();
  constexpr int kRootNodePly = 0;

  // WARNING: this fallback runs before the search timer starts, eating into
  // the move time budget. The cost is subtracted from search_time_ below.
  auto fallback_start = high_resolution_clock::now();
  vector<Move> fallback_moves = GenerateMoves();
  for (const Move& m : fallback_moves) {
    try {
      board_->MakeMove(m);
      best_move = m;
      board_->UnmakeMove(m);
      break;
    } catch (BadMove&) {
      continue;
    }
  }
  auto fallback_end = high_resolution_clock::now();
  float fallback_secs =
      std::chrono::duration<float>(fallback_end - fallback_start).count();
  search_time_ = std::max(0.01f, search_time_ - fallback_secs);

  search_start_ = high_resolution_clock::now();
  nodes_since_time_check_ = 0;
#ifdef BENCHMARK
  total_nodes_ = 0;
#endif
  // Set the first evaluation guess as an even game.
  int f = 0;
  int search_depth = 1;
  for (; search_depth <= kSearchLimit; ++search_depth) {
    try {
      f = MtdfSearch(f, search_depth, kRootNodePly, move);
      if (move.moving_piece != kNA || move.castling_type != kNA) {
        best_move = move;
      }
    } catch (OutOfTime& e) {
      break;
    }
  }

#ifdef BENCHMARK
  {
    search_depth =
      (search_depth == kSearchLimit) ? kSearchLimit : search_depth - 1;
    uint64_t nodes = total_nodes_ + nodes_since_time_check_;
    float elapsed = duration_cast<duration<float>>(
        high_resolution_clock::now() - search_start_).count();
    uint64_t nps = elapsed > 0 ? static_cast<uint64_t>(nodes / elapsed) : 0;
    std::cerr << "SEARCH DEPTH: " << search_depth
              << "  NODES: " << nodes << "  NPS: " << nps << endl;
  }
#endif

  board_->ResetPos();
  // Discard any hashes stranded by OutOfTime.
  pos_history_.resize(saved_history_size);
  assert(best_move.moving_piece != kNA || best_move.castling_type != kNA ||
         GetGameStatus() == kPlayerCheckmated || GetGameStatus() == kDraw);
  return best_move;
}

auto Engine::GetBestMove(int& score_out) -> Move {
  assert(!pos_history_.empty());
  board_->ClearPawnTable();
  for (auto& km : killer_moves_) km = {};
  size_t saved_history_size = pos_history_.size();
  Move best_move;
  Move move;
  board_->SavePos();
  constexpr int kRootNodePly = 0;

  auto fallback_start = high_resolution_clock::now();
  vector<Move> fallback_moves = GenerateMoves();
  for (const Move& m : fallback_moves) {
    try {
      board_->MakeMove(m);
      best_move = m;
      board_->UnmakeMove(m);
      break;
    } catch (BadMove&) {
      continue;
    }
  }
  auto fallback_end = high_resolution_clock::now();
  float fallback_secs =
      std::chrono::duration<float>(fallback_end - fallback_start).count();
  search_time_ = std::max(0.01f, search_time_ - fallback_secs);

  search_start_ = high_resolution_clock::now();
  nodes_since_time_check_ = 0;
#ifdef BENCHMARK
  total_nodes_ = 0;
#endif
  int f = 0;
  int search_depth = 1;
  for (; search_depth <= kSearchLimit; ++search_depth) {
    try {
      f = MtdfSearch(f, search_depth, kRootNodePly, move);
      if (move.moving_piece != kNA || move.castling_type != kNA) {
        best_move = move;
      }
    } catch (OutOfTime& e) {
      break;
    }
  }

#ifdef BENCHMARK
  {
    search_depth =
      (search_depth == kSearchLimit) ? kSearchLimit : search_depth - 1;
    uint64_t nodes = total_nodes_ + nodes_since_time_check_;
    float elapsed = duration_cast<duration<float>>(
        high_resolution_clock::now() - search_start_).count();
    uint64_t nps = elapsed > 0 ? static_cast<uint64_t>(nodes / elapsed) : 0;
    std::cerr << "SEARCH DEPTH: " << search_depth
              << "  NODES: " << nodes << "  NPS: " << nps << endl;
  }
#endif

  score_out = f;
  board_->ResetPos();
  pos_history_.resize(saved_history_size);
  assert(best_move.moving_piece != kNA || best_move.castling_type != kNA ||
         GetGameStatus() == kPlayerCheckmated || GetGameStatus() == kDraw);
  return best_move;
}

auto Engine::GetGameStatus() -> S8 {
  // Check for checks, checkmates, and draws.
  vector<Move> move_list = GenerateMoves();
  bool no_legal_moves = true;
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that leave the king in check.
      continue;
    }
    board_->UnmakeMove(move);
    no_legal_moves = false;
    break;
  }

  if (board_->KingInCheck()) {
    string player_name = GetPlayerStr(board_->GetPlayerToMove());
    if (no_legal_moves) {
      return kPlayerCheckmated;
    }
    return kPlayerInCheck;
  } else if (no_legal_moves) {
    return kDraw;
  }

  // Enforce the Fifty Move Rule.
  constexpr S8 kHalfmoveClockLimit = 100;
  if (board_->GetHalfmoveClock() >= kHalfmoveClockLimit) {
    return kDraw;
  }
  return kPlayerToMove;
}

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
    } catch (BadMove& e) {
      // Ignore all moves that put the player's king in check.
      continue;
    }
    node_count += Perft(depth - 1);
    board_->UnmakeMove(move);
  }
  return node_count;
}

auto Engine::GenerateMoves(bool captures_only) const -> vector<Move> {
  S8 moving_piece;
  S8 moving_player = board_->GetPlayerToMove();
  S8 enemy_player = GetOtherPlayer(moving_player);
  S8 start_sq;
  Bitboard moving_pieces = board_->GetPiecesByType(kNA, moving_player);
  Bitboard remove_bad_sqs_mask;
  vector<Move> move_list;
  if (captures_only) {
    // Remove all squares not occupied by the enemy player when generating
    // captures only.
    remove_bad_sqs_mask = board_->GetPiecesByType(kNA, enemy_player);
  } else {
    remove_bad_sqs_mask = ~moving_pieces;
    AddCastlingMoves(move_list);
  }

  AddEpMoves(move_list, enemy_player, moving_player);
  // Loop over all pieces from the moving player.
  while (moving_pieces) {
    // Generate attack maps for each piece.
    start_sq = GetSqOfFirstPiece(moving_pieces);
    moving_piece = board_->GetPieceOnSq(start_sq);
    assert(moving_piece >= kPawn && moving_piece <= kKing);
    Bitboard attack_map =
        board_->GetAttackMap(moving_player, start_sq, moving_piece);
    // Remove all invalid squares in the attack map.
    attack_map &= remove_bad_sqs_mask;
    AddMovesForPiece(move_list, attack_map, enemy_player, moving_player,
                     moving_piece, start_sq);
    RemoveFirstPiece(moving_pieces);
  }

  return move_list;
}

// Implement private member functions.

auto Engine::MtdfSearch(int f, int d, int ply, Move& best_move) -> int {
  assert(d >= 1);
  // Perform the MTD(f) algorithm, where f is the first guess for best value,
  // d is the depth to loop for, and g is the current guess.
  int g = f;
  int upper_bound = kBestEval;
  int lower_bound = kWorstEval;
  int beta;
  while (lower_bound < upper_bound) {
    if (g == lower_bound) {
      beta = g + 1;
    } else {
      beta = g;
    }
    g = NegamaxSearch(best_move, beta - 1, beta, d, ply, true);
    if (g < beta) {
      upper_bound = g;
    } else {
      lower_bound = g;
    }
  }
  return g;
}

auto Engine::ProbeTt(Move& pv_move, int& alpha, int& beta, int depth,
                     int& result) -> bool {
  int stored_eval;
  S8 node_type;
  if (!transposition_table_.Access(board_, depth, stored_eval, node_type)) {
    return false;
  }
  if (node_type == kPvNode) {
    pv_move = transposition_table_.GetHashMove(board_);
    result = stored_eval;
    return true;
  }
  if (node_type == kCutNode) {
    alpha = max(alpha, stored_eval);
  } else if (node_type == kAllNode) {
    beta = min(beta, stored_eval);
  }
  if (alpha >= beta) {
    Move hash_move = transposition_table_.GetHashMove(board_);
    if (hash_move.moving_piece != kNA || hash_move.castling_type != kNA) {
      pv_move = hash_move;
    }
    result = stored_eval;
    return true;
  }
  return false;
}

auto Engine::TryNullMovePrune(int alpha, int beta, int depth, int ply,
                               bool at_pv_node, bool in_check) -> bool {
  constexpr int kNullMoveDepthMin = 4;
  constexpr int kNullMoveDepthHighR = 6;
  if (depth < kNullMoveDepthMin || at_pv_node || !ZugzwangUnlikely()
      || in_check) {
    return false;
  }
  board_->MakeNullMove();
  int R = (depth > kNullMoveDepthHighR) ? 3 : 2;
  int null_move_eval = -NegamaxSearch(-beta, -alpha, depth - R - 1, ply + 1,
                                      false);
  board_->UnmakeNullMove();
  return null_move_eval >= beta;
}

auto Engine::ComputeStaticEval(int depth, bool at_pv_node,
                                bool in_check) -> int {
  if (depth <= kMaxFutilityPruningDepth && !at_pv_node && !in_check) {
    return board_->Evaluate();
  }
  return kWorstEval;
}

auto Engine::TryReverseFutilityPrune(int static_eval, int depth, int beta,
                                      bool at_pv_node, bool in_check) -> bool {
  if (depth > 2 || at_pv_node || in_check) {
    return false;
  }
  return static_eval - depth * kFutilityMargin >= beta;
}

auto Engine::ComputeLmrReduction(int depth, int legal_moves,
                                  S8 player_to_move,
                                  const Move& move) -> int {
  int reduction = static_cast<int>(sqrt(static_cast<double>(depth - 1)) +
                                   sqrt(static_cast<double>(legal_moves - 1)));
  int history_score =
      history_heuristic_[player_to_move][move.moving_piece][move.target_sq];
  if (history_score > 0) {
    reduction -= 1;
  } else if (history_score < kHistoryLmrThreshold) {
    reduction += 1;
  }
  return max(1, reduction);
}

auto Engine::RecordBetaCutoff(const Move& move, int depth, int ply,
                               const vector<Move>& searched_quiet_moves)
    -> void {
  if (move.captured_piece != kNA) {
    return;
  }
  RecordKillerMove(move, ply);
  if (move.castling_type == kNA) {
    Move prev_move;
    if (board_->GetPrevMove(prev_move) && prev_move.castling_type == kNA) {
      countermove_table_[prev_move.moving_piece][prev_move.target_sq] = move;
    }
    UpdateHistoryHeuristic(move, depth * depth);
    for (const Move& quiet_move : searched_quiet_moves) {
      UpdateHistoryHeuristic(quiet_move, -depth * depth);
    }
  }
}

auto Engine::StoreTtEntry(int best_eval, int orig_alpha, int beta, int depth,
                           const Move& best_move) -> void {
  if (best_eval <= orig_alpha) {
    transposition_table_.Update(board_, depth, best_eval, kAllNode);
  } else if (best_eval >= beta) {
    transposition_table_.Update(board_, depth, best_eval, kCutNode, best_move);
  } else {
    transposition_table_.Update(board_, depth, best_eval, kPvNode, best_move);
  }
}

auto Engine::NegamaxSearch(Move& pv_move, int alpha, int beta, int depth,
                           int ply, bool null_move_allowed) -> int {
  assert(ply >= 0 && ply < kSearchLimit);
  assert(alpha < beta);
  CheckSearchTime();

  int orig_alpha = alpha;
  int tt_result;
  if (ProbeTt(pv_move, alpha, beta, depth, tt_result)) {
    return tt_result;
  }

  constexpr S8 kHalfmoveClockLimit = 100;
  if (board_->GetHalfmoveClock() >= kHalfmoveClockLimit
      || (ply > 0 && RepDetected())) {
    return kNeutralEval;
  }
  if (depth <= 0) {
    return QuiescenceSearch(alpha, beta);
  }

  bool in_check = board_->KingInCheck();
  bool at_pv_node = transposition_table_.PosIsPvNode(board_);

  if (null_move_allowed && TryNullMovePrune(alpha, beta, depth, ply,
                                             at_pv_node, in_check)) {
    return beta;
  }

  int static_eval = ComputeStaticEval(depth, at_pv_node, in_check);
  if (static_eval == kWorstEval && depth <= 2 && !at_pv_node && !in_check) {
    static_eval = board_->Evaluate();
  }
  if (TryReverseFutilityPrune(static_eval, depth, beta, at_pv_node, in_check)) {
    return beta;
  }

  // --- Move loop ---
  vector<Move> move_list = GenerateMoves();
  move_list = OrderMoves(move_list, ply);
  vector<Move> searched_quiet_moves;
  size_t history_size_before_moves = pos_history_.size();
  S8 player_to_move = board_->GetPlayerToMove();
  Move best_move;
  int best_eval = kWorstEval;
  int legal_moves = 0;
  bool futility_pruned = false;

  for (size_t move_idx = 0; move_idx < move_list.size(); ++move_idx) {
    Move move = move_list[move_idx];

    if (ShouldFutilityPrune(move, static_eval, depth, at_pv_node, in_check,
                           alpha)) {
      futility_pruned = true;
      continue;
    }

    int see_val = kWorstEval;
    if (move.captured_piece != kNA) {
      see_val = board_->GetSee(move);
    }

    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      continue;
    }
    ++legal_moves;
    AddPosToHistory();
    bool gives_check = board_->KingInCheck();

    int num_quiet_searched = static_cast<int>(searched_quiet_moves.size());
    if (ShouldLateMovePrune(move, num_quiet_searched, depth, at_pv_node,
                            gives_check, in_check, ply)) {
      board_->UnmakeMove(move);
      pos_history_.resize(history_size_before_moves);
      continue;
    }

    if (ShouldSeePrune(move, depth, at_pv_node, gives_check, in_check,
                       see_val)) {
      board_->UnmakeMove(move);
      pos_history_.resize(history_size_before_moves);
      continue;
    }

    // Late move reduction or full-depth search.
    int search_eval;
    bool needs_full_search = true;

    if (legal_moves > kNumEarlyMoves && !at_pv_node
        && move.castling_type == kNA && move.promoted_to_piece == kNA
        && !gives_check && depth >= kMinReductionDepth) {
      bool should_reduce = (move.captured_piece == kNA);
      if (!should_reduce && move.captured_piece != kNA) {
        should_reduce = (see_val < 0);
      }
      if (should_reduce) {
        int depth_reduction =
            ComputeLmrReduction(depth, legal_moves, player_to_move, move);
        search_eval = -NegamaxSearch(-beta, -alpha, depth - depth_reduction - 1,
                                     ply + 1, true);
        if (search_eval > alpha) {
          search_eval = -NegamaxSearch(-beta, -alpha, depth - 1, ply + 1, true);
        }
        needs_full_search = false;
      }
    }

    if (needs_full_search) {
      S8 check_ext = gives_check ? 1 : 0;
      search_eval =
          -NegamaxSearch(-beta, -alpha, depth - 1 + check_ext, ply + 1, true);
    }

    board_->UnmakeMove(move);
    pos_history_.resize(history_size_before_moves);

    if (search_eval > best_eval) {
      best_move = move;
      pv_move = best_move;
      best_eval = search_eval;
    }

    alpha = max(alpha, search_eval);
    if (alpha >= beta) {
      RecordBetaCutoff(move, depth, ply, searched_quiet_moves);
      break;
    }

    if (move.captured_piece == kNA && move.castling_type == kNA) {
      searched_quiet_moves.push_back(move);
    }
  }

  if (legal_moves == 0) {
    if (futility_pruned) {
      return alpha;
    }
    return board_->KingInCheck() ? kWorstEval : kNeutralEval;
  }

  StoreTtEntry(best_eval, orig_alpha, beta, depth, best_move);
  return best_eval;
}

constexpr int kDelta = 900;

auto Engine::QuiescenceSearch(int alpha, int beta, int qs_depth) -> int {
  assert(alpha < beta);
  CheckSearchTime();

  constexpr S8 kHalfmoveClockLimit = 100;
  if (board_->GetHalfmoveClock() >= kHalfmoveClockLimit || RepDetected()) {
    return kNeutralEval;
  }

  bool in_check = board_->KingInCheck();

  if (qs_depth <= 0) {
    return board_->Evaluate();
  }

  if (!in_check) {
    // Establish a lower bound for the node evaluation (stand_pat_eval),
    // and perform a beta cutoff if this value exceeds beta.
    int stand_pat_eval = board_->Evaluate();
    if (stand_pat_eval >= beta) {
      return beta;
    }
    alpha = max(stand_pat_eval, alpha);

    if (!InEndgame()) {
      // Perfrom Delta Pruninhg if the position is extremely poor. It is assumed
      // it won't improve enough to exceed alpha.
      if (stand_pat_eval < alpha - kDelta) {
        return alpha;
      }
    }
  }

  // When in check, search all evasions. Otherwise, search captures only.
  vector<Move> move_list = GenerateMoves(/* captures_only = */ !in_check);
  move_list = OrderMoves(move_list);
  size_t history_size_before_qmoves = pos_history_.size();
  int legal_moves = 0;
  for (const Move& move : move_list) {
    // Skip searching captures that are likely to lose material when not in check.
    if (!in_check && board_->GetSee(move) < 0) {
      continue;
    }

    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      continue;
    }
    ++legal_moves;
    AddPosToHistory();
    int eval = -QuiescenceSearch(-beta, -alpha, qs_depth - 1);
    board_->UnmakeMove(move);
    pos_history_.resize(history_size_before_qmoves);

    if (eval >= beta) {
      return beta;
    }
    alpha = max(eval, alpha);
  }

  if (in_check && legal_moves == 0) {
    return kWorstEval;
  }

  return alpha;
}

constexpr int kCountermoveBonus = 5000;

auto Engine::OrderMoves(const vector<Move>& move_list, int ply) const
    -> vector<Move> {
  Move hash_move = transposition_table_.GetHashMove(board_);

  vector<pair<Move, int>> high_see_capture_pairs;
  vector<pair<Move, int>> low_see_capture_pairs;
  vector<pair<Move, int>> history_silent_move_pairs;
  vector<Move> killer_moves;
  vector<Move> ordered_moves;
  ordered_moves.reserve(move_list.size());
  int see_val;
  for (const Move& move : move_list) {
    // Prioritize a move if it's the previously calculated best move of a
    // node.
    if (move == hash_move) {
      ordered_moves.push_back(move);
    } else if (move.captured_piece != kNA) {
      // Use the SEE heuristic to order captures.
      see_val = board_->GetSee(move);
      if (see_val >= 0) {
        high_see_capture_pairs.emplace_back(move, see_val);
      } else {
        low_see_capture_pairs.emplace_back(move, see_val);
      }
    } else if (IsKillerMove(move, ply)) {
      // Use the Killer Move heuristic to order quiet moves.
      killer_moves.push_back(move);
    } else if (move.castling_type != kNA) {
      history_silent_move_pairs.emplace_back(move, 0);
    } else {
      // Use history and countermove heuristics to order silent, non-killer moves.
      S8 player_to_move = board_->GetPlayerToMove();
      int move_bonus = history_heuristic_[player_to_move][move.moving_piece][move.target_sq];
      Move prev_move;
      if (board_->GetPrevMove(prev_move) && prev_move.castling_type == kNA
          && move == countermove_table_[prev_move.moving_piece][prev_move.target_sq]) {
        move_bonus += kCountermoveBonus;
      }
      history_silent_move_pairs.emplace_back(
        move, move_bonus);
    }
  }

  // Sort captures by descending value of their SEE heuristic.
  sort(high_see_capture_pairs.begin(), high_see_capture_pairs.end(),
       [](const pair<Move, int>& lhs, const pair<Move, int>& rhs) {
         return lhs.second > rhs.second;
       });
  vector<Move> good_captures;
  good_captures.reserve(high_see_capture_pairs.size());
  for (const pair<Move, int>& capture_eval_pair : high_see_capture_pairs) {
    good_captures.push_back(capture_eval_pair.first);
  }
  sort(low_see_capture_pairs.begin(), low_see_capture_pairs.end(),
      [](const pair<Move, int>& lhs, const pair<Move, int>& rhs) {
        return lhs.second > rhs.second;
      });
  vector<Move> bad_captures;
  bad_captures.reserve(low_see_capture_pairs.size());
  for (const pair<Move, int>& capture_eval_pair : low_see_capture_pairs) {
    bad_captures.push_back(capture_eval_pair.first);
  }
  
  // Sort silent, non-killer moves by descending value of their history heuristic.
  sort(history_silent_move_pairs.begin(), history_silent_move_pairs.end(),
    [](const pair<Move, int>& lhs, const pair<Move, int>& rhs) {
      return lhs.second > rhs.second;
    });
  vector<Move> silent_moves;
  silent_moves.reserve(history_silent_move_pairs.size());
  for (const pair<Move, int>& history_silent_pair : history_silent_move_pairs) {
    silent_moves.push_back(history_silent_pair.first);
  }

  // Place all hash moves first, followed by good captures, then killer moves,
  // then all silent, non-killer moves, and finally bad captures.
  ordered_moves.insert(ordered_moves.end(), good_captures.begin(), good_captures.end());
  ordered_moves.insert(ordered_moves.end(), killer_moves.begin(),
                       killer_moves.end());
  ordered_moves.insert(ordered_moves.end(), silent_moves.begin(),
                       silent_moves.end());
  ordered_moves.insert(ordered_moves.end(), bad_captures.begin(), bad_captures.end());
  return ordered_moves;
}

// Implement an overloaded version of OrderMoves for Quescence Search.
auto Engine::OrderMoves(const vector<Move>& move_list) const -> vector<Move> {
  vector<pair<Move, int>> ordered_capture_pairs;
  vector<Move> late_moves;
  for (const Move& move : move_list) {
    if (move.captured_piece == kNA) {
      late_moves.push_back(move);
    } else {
      // Use the MVV-LVA heuristic to order captures.
      ordered_capture_pairs.emplace_back(
          move, kVictimSortVals[move.captured_piece] +
                    kAggressorSortVals[move.moving_piece]);
    }
  }
  // Sort captures by descending value of their MVV-LVA heuristic.
  sort(ordered_capture_pairs.begin(), ordered_capture_pairs.end(),
       [](const pair<Move, int>& lhs, const pair<Move, int>& rhs) {
         return lhs.second > rhs.second;
       });

  vector<Move> captures;
  captures.reserve(ordered_capture_pairs.size());
  for (const pair<Move, int>& capture_eval_pair : ordered_capture_pairs) {
    captures.push_back(capture_eval_pair.first);
  }

  // Place captures first, followed by all other moves.
  vector<Move> ordered_moves;
  ordered_moves.reserve(move_list.size());
  ordered_moves.insert(ordered_moves.end(), captures.begin(), captures.end());
  ordered_moves.insert(ordered_moves.end(), late_moves.begin(),
                       late_moves.end());
  return ordered_moves;
}

auto Engine::AddCastlingMoves(vector<Move>& move_list) const -> void {
  if (board_->CastlingLegal(kQueenSide)) {
    Move queenside_castle;
    queenside_castle.castling_type = kQueenSide;
    move_list.push_back(queenside_castle);
  }
  if (board_->CastlingLegal(kKingSide)) {
    Move kingside_castle;
    kingside_castle.castling_type = kKingSide;
    move_list.push_back(kingside_castle);
  }
}

auto Engine::AddEpMoves(vector<Move>& move_list, S8 enemy_player,
                        S8 moving_player) const -> void {
  S8 ep_target_sq = board_->GetEpTargetSq();
  if (ep_target_sq == kNA) return;

  // Capture only diagonal squares to En Passent target sq in the direction of
  // movement.
  Bitboard potential_ep_pawns;
  if (enemy_player == kWhite) {
    potential_ep_pawns = kNonSliderAttackMaps[kWhitePawnCapture][ep_target_sq];
  } else {
    potential_ep_pawns = kNonSliderAttackMaps[kBlackPawnCapture][ep_target_sq];
  }

  // Get the squares pawns can move from onto the en passent target square.
  // Note that because the target square is set, a single pawn push onto the
  // target square won't be possible, so this case can be safely ignored.
  Bitboard attack_map =
      potential_ep_pawns & board_->GetPiecesByType(kPawn, moving_player);
  if (attack_map) {
    Move ep;
    ep.is_ep = true;
    ep.moving_piece = kPawn;
    ep.target_sq = ep_target_sq;
    while (attack_map) {
      ep.start_sq = GetSqOfFirstPiece(attack_map);
      ep.captured_piece = kPawn;
      move_list.push_back(ep);
      RemoveFirstPiece(attack_map);
    }
  }
}

auto Engine::AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                              S8 enemy_player, S8 moving_player,
                              S8 moving_piece, S8 start_sq) const -> void {
  // Loop over all set bits in the attack map, with each representing
  // one elligible target square for a move.
  S8 player_on_target_sq;
  S8 start_rank;
  S8 start_file;
  S8 target_rank;
  S8 target_file;
  for (; attack_map; RemoveFirstPiece(attack_map)) {
    Move move;
    move.moving_piece = moving_piece;
    move.start_sq = start_sq;
    move.target_sq = GetSqOfFirstPiece(attack_map);

    // Check for captures.
    player_on_target_sq = board_->GetPlayerOnSq(move.target_sq);
    if (player_on_target_sq == enemy_player) {
      move.captured_piece = board_->GetPieceOnSq(move.target_sq);
    }

    if (moving_piece == kPawn) {
      start_rank = GetRankFromSq(move.start_sq);
      start_file = GetFileFromSq(move.start_sq);
      target_rank = GetRankFromSq(move.target_sq);
      target_file = GetFileFromSq(move.target_sq);

      if (start_file == target_file && move.captured_piece != kNA) {
        continue;
      }

      if (moving_player == kWhite) {
        if (start_rank == kRank2 && target_rank == kRank4) {
          if (board_->DoublePawnPushLegal(target_file)) {
            move.new_ep_target_sq = GetSqFromRankFile(kRank3, target_file);
          } else {
            continue;
          }
        } else if (target_rank == kRank8) {
          for (S8 piece = kKnight; piece <= kQueen; ++piece) {
            move.promoted_to_piece = piece;
            move_list.push_back(move);
          }
          continue;
        }
      } else if (moving_player == kBlack) {
        if (start_rank == kRank7 && target_rank == kRank5) {
          if (board_->DoublePawnPushLegal(target_file)) {
            move.new_ep_target_sq = GetSqFromRankFile(kRank6, target_file);
          } else {
            continue;
          }
        } else if (target_rank == kRank1) {
          for (S8 piece = kKnight; piece <= kQueen; ++piece) {
            move.promoted_to_piece = piece;
            move_list.push_back(move);
          }
          continue;
        }
      }
    }
    move_list.push_back(move);
  }
}

}  // namespace omegazero
