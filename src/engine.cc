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
#include <cstring>
#include <utility>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "game.h"
#include "move.h"
#include "out_of_time.h"
#include "syzygy.h"
#include "transposition_table.h"

namespace omegazero {

// Map a Syzygy WDL result to a search score, root-relative at `ply` (shorter
// wins/losses rank better). Cursed wins and blessed losses are 50-move draws.
static auto TbWdlToScore(TbWdl wdl, int ply) -> int {
  switch (wdl) {
    case TbWdl::kWin:
      return kTbWin - ply;
    case TbWdl::kLoss:
      return -kTbWin + ply;
    default:  // kDraw, kCursedWin, kBlessedLoss
      return kNeutralEval;
  }
}

using std::begin;
using std::end;
using std::fill;
using std::max;
using std::min;
using std::pair;
using std::sort;
using std::vector;
using std::chrono::high_resolution_clock;

// --- Public member functions ---

Engine::Engine(TranspositionTable* tt, Board* board, S8 player_side,
               float search_time) {
  assert(tt != nullptr);
  assert(board != nullptr);
  improving_ = false;
  transposition_table_ = tt;
  board_ = board;

  constexpr float kMinSearchTime = 0.1f;
  if (search_time < kMinSearchTime) {
    throw invalid_argument("Search time must be at least 0.1s");
  }
  soft_time_ = search_time;
  hard_time_ = search_time;
  base_time_ = search_time;
  dynamic_tm_ = false;
  depth_limit_ = kSearchLimit;
  node_limit_ = UINT64_MAX;
  stop_requested_.store(false);
  mate_target_ = 0;

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

  memset(history_heuristic_, 0, sizeof(history_heuristic_));
  memset(continuation_history_, 0, sizeof(continuation_history_));
  memset(correction_history_, 0, sizeof(correction_history_));
  memset(capture_history_, 0, sizeof(capture_history_));
  fill(begin(eval_history_), end(eval_history_), kInvalidEval);
}

Engine::Engine(TranspositionTable* tt, Board* board, S8 player_side,
               float search_time, const vector<U64>& pos_history) {
  assert(tt != nullptr);
  assert(board != nullptr);
  improving_ = false;
  transposition_table_ = tt;
  board_ = board;

  constexpr float kMinSearchTime = 0.1f;
  if (search_time < kMinSearchTime) {
    throw invalid_argument("Search time must be at least 0.1s");
  }
  soft_time_ = search_time;
  hard_time_ = search_time;
  base_time_ = search_time;
  dynamic_tm_ = false;
  depth_limit_ = kSearchLimit;
  node_limit_ = UINT64_MAX;
  stop_requested_.store(false);
  mate_target_ = 0;

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

  memset(history_heuristic_, 0, sizeof(history_heuristic_));
  memset(continuation_history_, 0, sizeof(continuation_history_));
  memset(correction_history_, 0, sizeof(correction_history_));
  memset(capture_history_, 0, sizeof(capture_history_));
  fill(begin(eval_history_), end(eval_history_), kInvalidEval);

  pos_history_ = pos_history;
}

constexpr int kRootNodePly = 0;

auto Engine::FirstLegalFallbackMove(const vector<Move>& moves) -> Move {
  for (const Move& m : moves) {
    // Honor `go searchmoves`: the fallback must also be a permitted root move.
    if (!search_moves_.empty()) {
      bool allowed = false;
      for (const Move& sm : search_moves_) {
        if (m == sm) {
          allowed = true;
          break;
        }
      }
      if (!allowed) {
        continue;
      }
    }
    try {
      board_->MakeMove(m);
      board_->UnmakeMove(m);
      return m;
    } catch (BadMove&) {
      continue;
    }
  }
  return Move{};
}

auto Engine::GetBestMove(int& score_out) -> Move {
  assert(!pos_history_.empty());
  board_->ClearPawnTable();
  for (auto& km : killer_moves_) km = {};
  size_t saved_history_size = pos_history_.size();
  board_->SavePos();

  // Pseudo-legal root moves; also the candidate list for the Syzygy probe and
  // the empty-search fallback below.
  vector<Move> fallback_moves = GenerateMoves();

  // Syzygy DTZ root probe: in a tablebase position, play the tablebase-optimal
  // move directly (perfect play, correct 50-move conversion) rather than
  // searching. No-op unless tables are loaded and the position qualifies.
  Move tb_move;
  if (ProbeTbRoot(fallback_moves, tb_move, score_out)) {
    board_->ResetPos();
    pos_history_.resize(saved_history_size);
    return tb_move;
  }

  InitTimeManagement();

  search_start_ = high_resolution_clock::now();
  nodes_since_time_check_ = 0;
  int prev_score = 0;
  int search_depth = 1;
  total_nodes_ = 0;
  iter_elapsed_[0] = 0.0f;
  completed_pv_len_ = 0;
  Move best_move;
  Move move = {};

  const int max_depth = min(depth_limit_, kSearchLimit);
  for (; search_depth <= max_depth; ++search_depth) {
    try {
      prev_score =
          AspirationSearch(prev_score, search_depth, kRootNodePly, move);
      if (!move.IsEmpty()) {
        best_move = move;
      }
    } catch (OutOfTime& e) {
      break;
    }

    // Record this iteration's result; the soft-bound rescale and stop decision
    // happen in UpdateSoftBoundAndShouldStop below.
    float elapsed = duration_cast<duration<float>>(
                        high_resolution_clock::now() - search_start_)
                        .count();
    root_best_history_[search_depth] = best_move;
    root_score_history_[search_depth] = prev_score;
    iter_elapsed_[search_depth] = elapsed;

    // Snapshot the completed root PV (pv_table_[0] gets reset by the next
    // iteration's entry, which may then abort before repopulating it).
    completed_pv_len_ = pv_length_[0];
    for (int pv_idx = 0; pv_idx < completed_pv_len_; ++pv_idx) {
      completed_pv_[pv_idx] = pv_table_[0][pv_idx];
    }

    // Report this completed depth (only completed depths reach here; an
    // OutOfTime abort breaks out above before this point).
    if (info_cb_) {
      SearchInfo info;
      info.depth = search_depth;
      info.score = prev_score;
      info.nodes = GetTotalNodes();
      info.time_ms = static_cast<long long>(elapsed * 1000.0f);
      info.pv = completed_pv_;
      info.pv_len = completed_pv_len_;
      info_cb_(info);
    }

    // UCI `go mate N`: stop once we can force mate in <= N moves.
    if (mate_target_ > 0 && prev_score > kBestEval - kSearchLimit) {
      int mate_moves = (kBestEval - prev_score + 1) / 2;
      if (mate_moves <= mate_target_) {
        break;
      }
    }

    if (UpdateSoftBoundAndShouldStop(search_depth, elapsed)) {
      break;
    }
  }

#ifdef BENCHMARK
  BenchmarkReport(search_depth == kSearchLimit ? kSearchLimit
                                               : search_depth - 1);
#endif

  score_out = prev_score;
  board_->ResetPos();
  pos_history_.resize(saved_history_size);

  // If the search was aborted before it ever assigned a best move (e.g. a UCI
  // `stop` or a node limit hit during the first root move), fall back to the
  // first legal root move so we never return an empty move. The board is back
  // at the root here, so this probe is a no-op in the common case.
  if (best_move.IsEmpty()) {
    best_move = FirstLegalFallbackMove(fallback_moves);
  }

  assert(!best_move.IsEmpty() || GetGameStatus() == kPlayerCheckmated ||
         GetGameStatus() == kDraw);
  return best_move;
}

constexpr S8 kHalfmoveClockLimit = 100;

auto Engine::GetGameStatus() -> S8 {
  // Check for checks, checkmates, and draws.
  vector<Move> move_list = GenerateMoves();
  bool no_made_moves_counter = true;
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that leave the king in check.
      continue;
    }
    board_->UnmakeMove(move);
    no_made_moves_counter = false;
    break;
  }

  if (board_->KingInCheck()) {
    string player_name = GetPlayerStr(board_->GetPlayerToMove());
    if (no_made_moves_counter) {
      return kPlayerCheckmated;
    }
    return kPlayerInCheck;
  } else if (no_made_moves_counter) {
    return kDraw;
  }

  // Enforce the Fifty Move Rule.
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

// --- Private member functions: dynamic time management ---

auto Engine::MoveStabilitySignal(int depth) const -> double {
  // Weighted fraction of recent iterations whose root best move changed from
  // the previous iteration, with recent changes weighted more heavily. Mapped
  // to [-1, +1]: fully stable across the window -> -1 (spend less), constantly
  // changing -> +1 (spend more).
  int window = std::min(depth - 1, params_.tm_window);
  if (window <= 0) {
    return 0.0;  // not enough history yet -> neutral
  }
  double changed_weight = 0.0;
  double total_weight = 0.0;
  double weight = 1.0;
  for (int offset = 0; offset < window; ++offset) {
    // Transition between completed depths `recent` and `recent - 1`.
    int recent = depth - offset;
    total_weight += weight;
    if (root_best_history_[recent] != root_best_history_[recent - 1]) {
      changed_weight += weight;
    }
    weight *= params_.tm_move_decay;
  }
  return 2.0 * (changed_weight / total_weight) - 1.0;
}

auto Engine::ScoreStabilitySignal(int depth) const -> double {
  // Combine the weighted mean absolute root-score change with an oscillation
  // (sign-flip) bonus, recent iterations weighted more. Mapped to [-1, +1]:
  // flat/smooth scores -> negative (spend less), large or oscillating -> +1.
  int window = std::min(depth - 1, params_.tm_window);
  if (window <= 0) {
    return 0.0;  // not enough history yet -> neutral
  }
  double weighted_abs = 0.0;
  double total_weight = 0.0;
  double oscillation = 0.0;
  double weight = 1.0;
  int prev_sign = 0;
  for (int offset = 0; offset < window; ++offset) {
    // Score delta between completed depths `recent` and `recent - 1`.
    int recent = depth - offset;
    int delta = root_score_history_[recent] - root_score_history_[recent - 1];
    weighted_abs += weight * std::abs(delta);
    total_weight += weight;
    int sign = (delta > 0) - (delta < 0);
    if (sign != 0 && prev_sign != 0 && sign != prev_sign) {
      oscillation += weight;
    }
    if (sign != 0) {
      prev_sign = sign;
    }
    weight *= params_.tm_move_decay;
  }
  double magnitude =
      min((weighted_abs / total_weight) / params_.tm_score_scale, 1.0);
  double instability = min(
      magnitude + params_.tm_osc_weight * (oscillation / total_weight), 1.0);
  return 2.0 * instability - 1.0;
}

auto Engine::DetectObviousRecapture() -> void {
  // An obvious recapture: the opponent's last move captured on some square, we
  // are not in check, and exactly one of our legal moves is a safe (SEE >= 0)
  // capture of that square. Such a move is nearly forced, so we can move on it
  // after only a couple of iterations.
  has_obvious_recapture_ = false;
  Move prev_move;
  if (!board_->GetPrevMove(prev_move) || prev_move.captured_piece == kNA ||
      board_->KingInCheck()) {
    return;
  }
  S8 captured_sq = prev_move.target_sq;
  Move recapture;
  int safe_recaptures = 0;
  vector<Move> moves = GenerateMoves();
  for (const Move& move : moves) {
    if (move.captured_piece == kNA || move.target_sq != captured_sq ||
        board_->GetSee(move) < 0) {
      continue;
    }
    try {
      board_->MakeMove(move);
    } catch (BadMove&) {
      continue;  // leaves our king in check; not a real recapture
    }
    board_->UnmakeMove(move);
    ++safe_recaptures;
    recapture = move;
  }
  if (safe_recaptures == 1) {
    has_obvious_recapture_ = true;
    obvious_recapture_ = recapture;
  }
}

auto Engine::InitTimeManagement() -> void {
  has_obvious_recapture_ = false;
  if (dynamic_tm_) {
    DetectObviousRecapture();
  }
  subtree_ema_init_ = false;
}

auto Engine::UpdateSoftBoundAndShouldStop(int depth, float elapsed) -> bool {
  // Rescale the soft bound by search difficulty (stable positions spend less,
  // unstable ones more). Then stop if the soft bound is crossed, or if the next
  // iteration is unlikely to finish before it.
  if (dynamic_tm_) {
    UpdateSubtreeShare();
    double difficulty = ComputeDifficulty(depth);
    soft_time_ = static_cast<float>(
        std::clamp(static_cast<double>(base_time_) * difficulty, 0.01,
                   static_cast<double>(hard_time_)));
  }
  if (elapsed >= soft_time_) {
    return true;
  }
  return dynamic_tm_ && PredictNextIterExceeds(depth);
}

auto Engine::UpdateSubtreeShare() -> void {
  // Fold the fraction of root nodes spent on the best move (from the last root
  // search) into an EMA. A dominant best move -> share near 1; effort spread
  // across root moves -> lower share.
  if (best_root_idx_ < 0 || best_root_idx_ >= root_move_count_) {
    return;
  }
  uint64_t total = 0;
  for (int move_idx = 0; move_idx < root_move_count_; ++move_idx) {
    total += root_move_nodes_[move_idx];
  }
  if (total == 0) {
    return;
  }
  double share = static_cast<double>(root_move_nodes_[best_root_idx_]) / total;
  if (!subtree_ema_init_) {
    subtree_share_ema_ = share;
    subtree_ema_init_ = true;
  } else {
    subtree_share_ema_ =
        params_.tm_subtree_ema_alpha * share +
        (1.0 - params_.tm_subtree_ema_alpha) * subtree_share_ema_;
  }
}

auto Engine::SubtreeStabilitySignal() const -> double {
  // Map best-move node share to [-1, +1]: dominant move (share -> 1) -> -1
  // (spend less), effort spread (share -> 0) -> +1 (spend more).
  if (!subtree_ema_init_) {
    return 0.0;
  }
  return 1.0 - 2.0 * subtree_share_ema_;
}

auto Engine::ComputeDifficulty(int depth) const -> double {
  // A found or faced forced mate: the result is decided, so move quickly.
  if (std::abs(root_score_history_[depth]) > kBestEval - kSearchLimit) {
    return params_.tm_mate_difficulty;
  }
  // An obvious recapture that has stayed best for at least one full iteration:
  // it is nearly forced, so spend very little.
  if (has_obvious_recapture_ && depth >= 2 &&
      root_best_history_[depth] == obvious_recapture_) {
    return params_.tm_obvious_difficulty;
  }
  double difficulty = 1.0 +
                      params_.tm_move_weight * MoveStabilitySignal(depth) +
                      params_.tm_score_weight * ScoreStabilitySignal(depth) +
                      params_.tm_subtree_weight * SubtreeStabilitySignal();
  return std::clamp(difficulty, params_.tm_difficulty_min,
                    params_.tm_difficulty_max);
}

auto Engine::PredictNextIterExceeds(int depth) const -> bool {
  if (depth < 2) {
    return false;  // need two completed iterations to estimate
  }
  float t_d = iter_elapsed_[depth] - iter_elapsed_[depth - 1];
  float t_prev = iter_elapsed_[depth - 1] - iter_elapsed_[depth - 2];
  double ebf = (t_prev > 0.0f) ? static_cast<double>(t_d) / t_prev
                               : params_.tm_ebf_fallback;
  ebf = std::clamp(ebf, params_.tm_ebf_min, params_.tm_ebf_max);
  double predicted_end = static_cast<double>(iter_elapsed_[depth]) + ebf * t_d;
  return predicted_end > static_cast<double>(soft_time_);
}

// --- Private member functions: search ---

auto Engine::AspirationSearch(int prev_score, int depth, int ply,
                              Move& best_move) -> int {
  assert(depth >= 1);
  if (depth == 1) {
    return Pvs(best_move, kWorstEval, kBestEval, depth, ply, true);
  }
  int alpha = max(prev_score - params_.aspiration_delta, kWorstEval);
  int beta = min(prev_score + params_.aspiration_delta, kBestEval);
  int delta = params_.aspiration_delta;

  for (;;) {
    int score = Pvs(best_move, alpha, beta, depth, ply, true);
    // Only widen a bound that isn't already pinned to the evaluation limit. A
    // decisive (mate) score can equal a clamped bound (e.g. score == beta ==
    // kBestEval), which would otherwise spin this loop forever and overflow
    // `delta` via repeated doubling, producing garbage bounds. `delta` is also
    // capped so it can never overflow.
    if (score <= alpha && alpha > kWorstEval) {
      delta = min(delta * 2, kBestEval);
      alpha = max(score - delta, kWorstEval);
    } else if (score >= beta && beta < kBestEval) {
      delta = min(delta * 2, kBestEval);
      beta = min(score + delta, kBestEval);
    } else {
      return score;
    }
  }
}

auto Engine::Pvs(Move& pv_move, int alpha, int beta, int depth, int ply,
                 bool null_move_allowed) -> int {
  // Start with an empty PV at this ply so any early return (TT cutoff, draw,
  // qsearch, prune) leaves no stale continuation for the parent to splice in.
  pv_length_[ply] = 0;
  if (ply >= kSearchLimit) {
    return board_->Evaluate();
  }
  CheckSearchTime();

  // Reset per-root-move node accounting so an early return (e.g. TT cutoff)
  // leaves no stale data for the dynamic-TM subtree signal.
  if (ply == kRootNodePly) {
    best_root_idx_ = -1;
    root_move_count_ = 0;
  }

  int orig_alpha = alpha;

  int tt_result;
  TableEntry hash_entry = transposition_table_->GetHashEntry(board_);
  if (ProbeTt(alpha, beta, depth, ply, tt_result)) {
    // Only surface the hash move as the PV move if it is actually legal in the
    // current position. An unvalidated hash move can be illegal here (e.g. a
    // stale/colliding entry), and at the root it would be returned and played
    // verbatim.
    const Move& hash_move = hash_entry.hash_move;
    if (hash_move.IsEmpty() || ValidateTtMove(hash_move)) {
      pv_move = hash_move;
      return tt_result;
    }
  }

  // Reduce the depth of the entire node if no hash move is present.
  if (depth >= params_.min_iir_depth && !ValidateTtMove(hash_entry.hash_move)) {
    --depth;
  }

  if (board_->GetHalfmoveClock() >= kHalfmoveClockLimit ||
      (ply > 0 && RepDetected())) {
    return kNeutralEval;
  }

  // Syzygy WDL probe: at a small, castling-free, rule50 == 0 position the
  // tablebase gives an exact result, returned as a cutoff. The root (ply 0) is
  // left to normal search (DTZ root probing is a later phase). No-op unless
  // tables are loaded, so the default build is unchanged.
  if (ply > 0 && ShouldProbeTb()) {
    TbWdl wdl = g_syzygy.ProbeWdl(*board_);
    if (wdl != TbWdl::kFailed) {
      return TbWdlToScore(wdl, ply);
    }
  }

  if (depth <= 0) {
    return QuiescenceSearch(alpha, beta, ply);
  }

  bool in_check = board_->KingInCheck();
  bool at_pv_node = transposition_table_->PosIsPvNode(board_);
  if (null_move_allowed &&
      ShouldNullMovePrune(alpha, beta, depth, ply, at_pv_node, in_check)) {
    return beta;
  }

  // Look for the first ply we weren't in check between 2 and 4 plies ago. If
  // the static eval has improved, or we were in check both 2 and 4 plies ago,
  // set the improving flag to true.
  int raw_static_eval = in_check ? kInvalidEval : board_->Evaluate();
  int static_eval = in_check ? kInvalidEval : GetCorrectedEval(raw_static_eval);
  eval_history_[ply] = static_eval;
  if (in_check)
    improving_ = false;
  else if (ply >= 2 && eval_history_[ply - 2] != kInvalidEval) {
    improving_ = static_eval > eval_history_[ply - 2];
  } else if (ply >= 4 && eval_history_[ply - 4] != kInvalidEval) {
    improving_ = static_eval > eval_history_[ply - 4];
  } else {
    improving_ = true;
  }

  // Drop into quiescence search immediately if the current position static
  // evalustion doesn't look promising.
  if (depth <= params_.max_razoring_depth && !at_pv_node && !in_check &&
      static_eval + params_.razoring_margin < alpha) {
    return QuiescenceSearch(alpha, beta, ply);
  }

  if (ShouldReverseFutilityPrune(static_eval, depth, beta, at_pv_node,
                                 in_check)) {
    return beta;
  }

  int singular_ext = TrySingularExtension(hash_entry, depth, ply, beta);
  if (singular_ext == kNA) {
    return beta;
  }

  vector<Move> move_list = GenerateMoves();
  move_list = OrderMoves(move_list, ply);
  // UCI `go searchmoves`: at the root, keep only the caller-permitted moves.
  if (ply == kRootNodePly && !search_moves_.empty()) {
    vector<Move> filtered;
    filtered.reserve(search_moves_.size());
    for (const Move& m : move_list) {
      for (const Move& allowed : search_moves_) {
        if (m == allowed) {
          filtered.push_back(m);
          break;
        }
      }
    }
    move_list = std::move(filtered);
  }
  vector<Move> searched_quiet_moves;
  vector<Move> searched_captures;
  size_t history_size_before_moves = pos_history_.size();
  S8 player_to_move = board_->GetPlayerToMove();
  Move best_move;
  int best_eval = kWorstEval;
  int made_moves_counter = 0;
  bool futility_pruned = false;
  // Only full-window (PV) nodes yield a real principal variation; skip the
  // splice on null-window nodes to keep it off the hot path.
  bool collect_pv = beta - alpha > 1;

  // Reset per-root-move node accounting for this root search (dynamic TM).
  if (ply == kRootNodePly) {
    root_move_count_ =
        std::min(static_cast<int>(move_list.size()), kMaxRootMoves);
    for (int move_idx = 0; move_idx < root_move_count_; ++move_idx) {
      root_move_nodes_[move_idx] = 0;
    }
    best_root_idx_ = -1;
  }

  // --- Move loop ---
  for (size_t move_idx = 0; move_idx < move_list.size(); ++move_idx) {
    Move move = move_list[move_idx];
    // Skip a candidate singular move during null window searches for singular
    // search extensions.
    if (move == excluded_move_) {
      continue;
    }

    if (ShouldFutilityPrune(move, static_eval, depth, at_pv_node, in_check,
                            alpha)) {
      futility_pruned = true;
      continue;
    }

    int see_val = kInvalidEval;
    if (move.captured_piece != kNA) {
      see_val = board_->GetSee(move);
    }

    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      continue;
    }
    ++made_moves_counter;
    AddPosToHistory();
    bool gives_check = board_->KingInCheck();
    int check_ext = gives_check ? 1 : 0;
    int ext = check_ext;
    if (move == hash_entry.hash_move) {
      ext = max(singular_ext, check_ext);
    }

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

    // Node count before this root move's subtree is searched (dynamic TM).
    uint64_t root_nodes_before = 0;
    if (ply == kRootNodePly) {
      root_nodes_before = GetTotalNodes();
    }
    int search_eval;
    if (made_moves_counter == 1) {
      // Search the first move (presumed to be the best) with a full window
      // and full depth.
      search_eval = -Pvs(-beta, -alpha, depth - 1 + ext, ply + 1, true);
    } else {
      // Search with a reduced depth and null window for moves that are late
      // and quiet to verify that the move probably isn't better than the
      // first move searched.
      bool reduced = false;
      if (made_moves_counter > params_.num_early_moves && !at_pv_node &&
          move.castling_type == kNA && move.promoted_to_piece == kNA &&
          !gives_check && depth >= params_.min_reduction_depth &&
          (move.captured_piece == kNA || see_val < 0)) {
        int depth_reduction = ComputeLmrReduction(depth, made_moves_counter,
                                                  player_to_move, move);
        search_eval = -Pvs(-alpha - 1, -alpha, depth - depth_reduction - 1,
                           ply + 1, true);
        reduced = true;
      }

      // Re-search with full depth if the move doesn't fail low, but with a
      // null window, indicating higher depth is needed to determine if this
      // could be a good move.
      if (!reduced || search_eval > alpha) {
        search_eval = -Pvs(-alpha - 1, -alpha, depth - 1 + ext, ply + 1, true);
      }

      // Re-search with the full window and full depth if the move doesn't
      // fail high or low on the full depth search, indicating the move
      // could be good.
      if (search_eval > alpha && search_eval < beta) {
        search_eval = -Pvs(-beta, -alpha, depth - 1 + ext, ply + 1, true);
      }
    }

    board_->UnmakeMove(move);
    pos_history_.resize(history_size_before_moves);
    if (ply == kRootNodePly && move_idx < static_cast<size_t>(kMaxRootMoves)) {
      root_move_nodes_[move_idx] += GetTotalNodes() - root_nodes_before;
    }

    if (search_eval > best_eval) {
      best_move = move;
      pv_move = best_move;
      best_eval = search_eval;
      if (ply == kRootNodePly) {
        best_root_idx_ = static_cast<int>(move_idx);
      }
      // A new alpha-raising best move at a PV node extends the principal
      // variation: this move followed by the child's PV (from the last
      // full-window search of it).
      if (collect_pv && search_eval > alpha) {
        pv_table_[ply][0] = move;
        int child_len = min(pv_length_[ply + 1], kSearchLimit - 1);
        for (int pv_idx = 0; pv_idx < child_len; ++pv_idx) {
          pv_table_[ply][pv_idx + 1] = pv_table_[ply + 1][pv_idx];
        }
        pv_length_[ply] = child_len + 1;
      }
    }

    alpha = max(alpha, search_eval);
    if (alpha >= beta) {
      RecordBetaCutoff(move, depth, ply, searched_quiet_moves,
                       searched_captures);
      break;
    }

    // Keep track of searched moves for history and capture maluses.
    if (move.castling_type == kNA) {
      if (move.captured_piece == kNA) {
        searched_quiet_moves.push_back(move);
      } else {
        searched_captures.push_back(move);
      }
    }
  }

  if (made_moves_counter == 0) {
    if (futility_pruned) {
      return alpha;
    }
    // Being checkmated `ply` plies from the root: a nearer mate is worse, so
    // encode the distance so the search prefers to be mated later.
    return board_->KingInCheck() ? kWorstEval + ply : kNeutralEval;
  }
  StoreTtEntry(best_eval, orig_alpha, beta, depth, ply, best_move);
  if (!in_check && best_eval < beta) {
    UpdateCorrectionHistory(raw_static_eval, best_eval, depth);
  }
  return best_eval;
}

auto Engine::QuiescenceSearch(int alpha, int beta, int ply, int qs_depth)
    -> int {
  assert(alpha < beta);
  CheckSearchTime();

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
      // Perform Delta Pruning if the position is extremely poor. It is
      // assumed it won't improve enough to exceed alpha.
      if (stand_pat_eval < alpha - params_.qs_delta) {
        return alpha;
      }
    }
  }

  // When in check, search all evasions. Otherwise, search captures only.
  vector<Move> move_list = GenerateMoves(/* captures_only = */ !in_check);
  move_list = OrderMoves(move_list);
  size_t history_size_before_qmoves = pos_history_.size();
  int made_moves_counter = 0;
  for (const Move& move : move_list) {
    // Skip searching captures that are likely to lose material when not in
    // check.
    if (!in_check && board_->GetSee(move) < 0) {
      continue;
    }

    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      continue;
    }
    ++made_moves_counter;
    AddPosToHistory();
    int eval = -QuiescenceSearch(-beta, -alpha, ply + 1, qs_depth - 1);
    board_->UnmakeMove(move);
    pos_history_.resize(history_size_before_qmoves);

    if (eval >= beta) {
      return beta;
    }
    alpha = max(eval, alpha);
  }

  if (in_check && made_moves_counter == 0) {
    return kWorstEval + ply;
  }

  return alpha;
}

auto Engine::TrySingularExtension(const TableEntry& hash_entry, int depth,
                                  int ply, int beta) -> int {
  bool not_in_extension = excluded_move_.IsEmpty();
  bool not_mate = abs(hash_entry.eval) < kBestEval - kSearchLimit;
  bool deep_enough = hash_entry.search_depth >= depth - 3;
  bool not_all_node =
      (hash_entry.node_type == kPvNode || hash_entry.node_type == kCutNode);

  bool should_extend =
      (depth >= params_.singular_depth_min && not_in_extension &&
       ValidateTtMove(hash_entry.hash_move) && not_mate && deep_enough &&
       not_all_node);
  if (should_extend) {
    int singular_beta = hash_entry.eval - 2 * depth;
    excluded_move_ = hash_entry.hash_move;
    int half_depth = (depth - 1) / 2;
    int singular_score =
        Pvs(singular_beta - 1, singular_beta, half_depth, ply + 1, false);
    excluded_move_ = Move{};

    if (singular_score < singular_beta) {
      return 1;
    } else if (singular_score >= beta) {
      return kNA;
    }
  }
  return 0;
}

// --- Private member functions: move ordering ---

constexpr int kCountermoveBonus = 5000;
constexpr int kCaptureSeeWeight = 32;

auto Engine::OrderMoves(const vector<Move>& move_list, int ply) const
    -> vector<Move> {
  Move hash_move = transposition_table_->GetHashMove(board_);

  vector<pair<Move, int>> high_see_capture_pairs;
  vector<pair<Move, int>> low_see_capture_pairs;
  vector<pair<Move, int>> history_silent_move_pairs;
  vector<Move> killer_moves;
  vector<Move> ordered_moves;
  ordered_moves.reserve(move_list.size());
  int see_val;
  S8 player_to_move = board_->GetPlayerToMove();
  for (const Move& move : move_list) {
    // Prioritize a move if it's the previously calculated best move of a
    // node.
    if (move == hash_move) {
      ordered_moves.push_back(move);
    } else if (move.promoted_to_piece != kNA) {
      int promotion_bonus =
          kCaptureSeeWeight *
          (kPieceVals[move.promoted_to_piece] - kPieceVals[kPawn]);
      high_see_capture_pairs.emplace_back(move, promotion_bonus);
    } else if (move.captured_piece != kNA) {
      see_val = board_->GetSee(move);
      int capture_history_val =
          capture_history_[player_to_move][move.moving_piece][move.target_sq]
                          [move.captured_piece];
      int capture_bonus = kCaptureSeeWeight * see_val + capture_history_val;
      if (see_val >= 0) {
        high_see_capture_pairs.emplace_back(move, capture_bonus);
      } else {
        low_see_capture_pairs.emplace_back(move, capture_bonus);
      }
    } else if (IsKillerMove(move, ply)) {
      // Use the Killer Move heuristic to order quiet moves.
      killer_moves.push_back(move);
    } else if (move.castling_type != kNA) {
      history_silent_move_pairs.emplace_back(move, 0);
    } else {
      // Use history and countermove heuristics to order silent, non-killer
      // moves.
      int move_bonus =
          history_heuristic_[player_to_move][move.moving_piece][move.target_sq];
      Move prev_move;
      if (board_->GetPrevMove(prev_move) && prev_move.castling_type == kNA) {
        // Add continuation history bonus.
        move_bonus +=
            continuation_history_[prev_move.moving_piece][prev_move.target_sq]
                                 [move.moving_piece][move.target_sq];

        // Add countermove history bonus.
        if (move ==
            countermove_table_[prev_move.moving_piece][prev_move.target_sq]) {
          move_bonus += kCountermoveBonus;
        }
      }
      history_silent_move_pairs.emplace_back(move, move_bonus);
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

  // Sort silent, non-killer moves by descending value of their history
  // heuristic.
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
  ordered_moves.insert(ordered_moves.end(), good_captures.begin(),
                       good_captures.end());
  ordered_moves.insert(ordered_moves.end(), killer_moves.begin(),
                       killer_moves.end());
  ordered_moves.insert(ordered_moves.end(), silent_moves.begin(),
                       silent_moves.end());
  ordered_moves.insert(ordered_moves.end(), bad_captures.begin(),
                       bad_captures.end());
  return ordered_moves;
}

constexpr int kAggressorSortVals[kNumPieceTypes] = {-1, -2, -3, -4, -5, -6};
constexpr int kVictimSortVals[kNumPieceTypes] = {10, 20, 30, 40, 50, 60};

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

// --- Private member functions: move generation helpers ---

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
          for (S8 piece = kQueen; piece >= kKnight; --piece) {
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
          for (S8 piece = kQueen; piece >= kKnight; --piece) {
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

// --- Private member functions: transposition table & pruning helpers ---

auto Engine::ProbeTt(int& alpha, int& beta, int depth, int ply, int& result)
    -> bool {
  int stored_eval;
  S8 node_type;
  if (!transposition_table_->ProbeEval(board_, depth, stored_eval, node_type)) {
    return false;
  }
  // Rebase a stored (node-relative) mate score back to this search's root.
  stored_eval = ScoreFromTt(stored_eval, ply);
  if (node_type == kPvNode) {
    result = stored_eval;
    return true;
  }
  if (node_type == kCutNode) {
    alpha = max(alpha, stored_eval);
  } else if (node_type == kAllNode) {
    beta = min(beta, stored_eval);
  }
  if (alpha >= beta) {
    result = stored_eval;
    return true;
  }
  return false;
}

auto Engine::TbPositionEligible() const -> bool {
  if (!g_syzygy.IsLoaded()) {
    return false;
  }
  // Syzygy positions have no castling rights.
  if (board_->GetCastlingRight(kWhite, kQueenSide) ||
      board_->GetCastlingRight(kWhite, kKingSide) ||
      board_->GetCastlingRight(kBlack, kQueenSide) ||
      board_->GetCastlingRight(kBlack, kKingSide)) {
    return false;
  }
  return g_syzygy.PieceCount(*board_) <= g_syzygy.MaxPieces();
}

auto Engine::ShouldProbeTb() const -> bool {
  // Fathom's WDL probe additionally requires a zeroed 50-move counter.
  return TbPositionEligible() && board_->GetHalfmoveClock() == 0;
}

auto Engine::ProbeTbRoot(const vector<Move>& root_moves, Move& tb_move,
                         int& score_out) -> bool {
  if (!TbPositionEligible()) {
    return false;
  }
  TbRootMove root;
  if (!g_syzygy.ProbeRoot(*board_, root)) {
    return false;
  }
  // Match the tablebase's from/to/promotion to a generated pseudo-legal move so
  // tb_move carries the full Move fields, and confirm the move is legal.
  for (const Move& move : root_moves) {
    if (move.start_sq != root.from_sq || move.target_sq != root.to_sq ||
        move.promoted_to_piece != root.promo_piece) {
      continue;
    }
    try {
      board_->MakeMove(move);
      board_->UnmakeMove(move);
    } catch (BadMove&) {
      continue;
    }
    tb_move = move;
    score_out = TbWdlToScore(root.wdl, kRootNodePly);
    return true;
  }
  return false;
}

auto Engine::ShouldNullMovePrune(int alpha, int beta, int depth, int ply,
                                 bool at_pv_node, bool in_check) -> bool {
  if (depth < params_.null_move_depth_min || at_pv_node ||
      !ZugzwangUnlikely() || in_check) {
    return false;
  }
  board_->MakeNullMove();
  int R = (depth > params_.null_move_depth_high_r) ? 3 : 2;
  // Increase reduction when the line being explored isn't improving.
  if (!improving_) {
    ++R;
  }
  int null_move_eval = -Pvs(-beta, -alpha, depth - R - 1, ply + 1, false);

  board_->UnmakeNullMove();
  return null_move_eval >= beta;
}

auto Engine::RecordBetaCutoff(const Move& move, int depth, int ply,
                              const vector<Move>& searched_quiet_moves,
                              const vector<Move>& searched_captures) -> void {
  // Penalize captures that were searched but didn't cause the cutoff.
  for (const Move& searched_capture : searched_captures) {
    UpdateCaptureHistory(searched_capture, -depth * depth);
  }
  // Reward captures that caused a beta-cutoff.
  if (move.captured_piece != kNA) {
    UpdateCaptureHistory(move, depth * depth);
    return;
  }
  RecordKillerMove(move, ply);
  if (move.castling_type == kNA) {
    Move prev_move;
    bool prev_move_exists =
        board_->GetPrevMove(prev_move) && prev_move.castling_type == kNA;
    if (prev_move_exists) {
      countermove_table_[prev_move.moving_piece][prev_move.target_sq] = move;
      UpdateContinuationHistory(prev_move, move, depth * depth);
    }
    UpdateHistoryHeuristic(move, depth * depth);
    for (const Move& quiet_move : searched_quiet_moves) {
      UpdateHistoryHeuristic(quiet_move, -depth * depth);
      if (prev_move_exists) {
        UpdateContinuationHistory(prev_move, quiet_move, -depth * depth);
      }
    }
  }
}

// --- Guarded: benchmark ---

#ifdef BENCHMARK
auto Engine::BenchmarkReport(int search_depth) -> void {
  uint64_t nodes = total_nodes_ + nodes_since_time_check_;
  float elapsed = duration_cast<duration<float>>(high_resolution_clock::now() -
                                                 search_start_)
                      .count();
  uint64_t nps = elapsed > 0 ? static_cast<uint64_t>(nodes / elapsed) : 0;
  std::cerr << "SEARCH DEPTH: " << search_depth << "  NODES: " << nodes
            << "  NPS: " << nps << endl;
}
#endif

}  // namespace omegazero
