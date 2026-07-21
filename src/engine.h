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
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "board.h"
#include "move.h"
#include "out_of_time.h"
#include "transposition_table.h"

namespace omegazero {

using std::begin;
using std::clamp;
using std::copy;
using std::end;
using std::invalid_argument;
using std::max;
using std::min;
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
constexpr int kCorrHistSize = 16384;
// Correction-history fixed-point grain and saturation bound. Kept compile-time
// (not runtime-tunable) because kCorrHistGrain divides per node in
// GetCorrectedEval -- as a power-of-two constant that is a shift, but a runtime
// value forces a real integer division (~8% NPS). See SearchParams.
constexpr int kCorrHistGrain = 256;
constexpr int kCorrHistMax = 256;

constexpr int kBestEval = 32000;
constexpr int kNeutralEval = -25;
constexpr int kWorstEval = -32000;
constexpr int kInvalidEval = 32001;

// Mate scores are encoded, relative to the search root, as kBestEval minus the
// number of plies until we deliver mate (or kWorstEval plus the plies until we
// are mated). Any score within kSearchLimit of the extremes is therefore a mate
// score. This band is well clear of ordinary centipawn evaluations.
inline auto IsMateScore(int score) -> bool {
  return score > kBestEval - kSearchLimit || score < kWorstEval + kSearchLimit;
}
// A mate score's magnitude encodes its distance from the search root, but the
// transposition table is keyed by position and may be reached at a different
// root distance later. ScoreToTt rebases a root-relative mate score to be
// relative to the node at `ply` (for storage); ScoreFromTt is its inverse (on
// retrieval). Non-mate scores pass through unchanged.
inline auto ScoreToTt(int score, int ply) -> int {
  if (score > kBestEval - kSearchLimit) return score + ply;
  if (score < kWorstEval + kSearchLimit) return score - ply;
  return score;
}
inline auto ScoreFromTt(int score, int ply) -> int {
  if (score > kBestEval - kSearchLimit) return score - ply;
  if (score < kWorstEval + kSearchLimit) return score + ply;
  return score;
}

// --- Dynamic time-management tuning ---
// Master switch for difficulty-scaled dynamic time management. Disabled for v4:
// an SPRT (dynamic vs. static soft/hard) showed the current tuning regressing
// ~10 Elo, so clock play falls back to the static bounds. The full mechanism
// below stays compiled and ready to be re-enabled and SPSA-tuned in v5.
constexpr bool kDynamicTmEnabled = false;
// Runtime-tunable search parameters, surfaced as UCI spin options (see the
// option tables in uci.cc) so they can be SPSA-tuned without a rebuild. Defaults
// match the historical constexpr values, so a fresh engine behaves exactly as
// before. Doubles are exposed to UCI as integers via a per-option divisor.
// (Structural constants -- array sizes, score sentinels, chess rules -- are not
// here because they cannot vary at runtime without breaking correctness.)
struct SearchParams {
  // --- Dynamic time management ---
  int tm_window = 5;               // recent iterations weighed for move stability
  double tm_move_decay = 0.6;      // geometric decay favoring recent changes
  double tm_move_weight = 0.5;     // weight of the best-move-stability term
  double tm_score_weight = 0.3;    // weight of the score-stability term
  double tm_score_scale = 100.0;   // cp swing mapping to full magnitude-instability
  double tm_osc_weight = 0.5;      // extra weight for score oscillation
  double tm_mate_difficulty = 0.5;      // difficulty once a mate is found/faced
  double tm_obvious_difficulty = 0.25;  // difficulty for an obvious recapture
  double tm_subtree_weight = 0.2;       // weight of the subtree/node-effort term
  double tm_subtree_ema_alpha = 0.5;    // EMA smoothing of the best-move node share
  double tm_difficulty_min = 0.45;      // clamp floor on the difficulty multiplier
  double tm_difficulty_max = 2.5;       // clamp ceiling on the difficulty multiplier
  double tm_ebf_min = 1.5;              // predictive early-stop EBF clamp floor
  double tm_ebf_max = 4.0;              // predictive early-stop EBF clamp ceiling
  double tm_ebf_fallback = 2.0;         // EBF used before two iterations complete
  // --- Pruning / reduction margins, depths, thresholds ---
  int aspiration_delta = 25;         // initial aspiration half-window (cp)
  int futility_margin = 200;         // per-depth (reverse) futility margin (cp)
  int max_futility_pruning_depth = 2;    // max depth for (reverse) futility pruning
  int max_late_move_pruning_depth = 2;   // max depth for late-move pruning
  int max_see_pruning_depth = 5;         // max depth for SEE pruning
  int see_margin = 100;              // per-depth SEE-pruning margin (cp)
  int history_lmr_threshold = -1000; // history below which LMR reduces one more
  int num_early_moves = 3;           // moves searched at full depth before LMR
  int min_reduction_depth = 3;       // min depth for late-move reductions
  int min_iir_depth = 4;             // min depth for internal iterative reduction
  int max_razoring_depth = 3;        // max depth for razoring
  int razoring_margin = 350;         // razoring drop-to-qsearch margin (cp)
  int singular_depth_min = 6;        // min depth for singular extensions
  int null_move_depth_min = 4;       // min depth for null-move pruning
  int null_move_depth_high_r = 6;    // depth above which NMP uses the larger R
  int qs_delta = 900;                // quiescence delta-pruning margin (cp)
};

// Upper bound on legal moves in a position (max ~218), sizing the per-root-move
// node-count table.
constexpr int kMaxRootMoves = 256;

// One completed iterative-deepening iteration's result, passed to the info
// callback (if one is registered) so a UCI front-end can emit an `info` line.
// The engine itself performs no I/O; formatting/printing lives in the caller.
struct SearchInfo {
  int depth;
  int score;        // Internal score: centipawns, or a mate score (IsMateScore).
  uint64_t nodes;
  long long time_ms;
  const Move* pv;   // Principal variation, pv[0] first; length is pv_len.
  int pv_len;
};

class Engine {
 public:
  Engine(Board* board, S8 player_side, float search_time);

  // Register a callback invoked once per completed depth during GetBestMove()
  // with that iteration's depth/score/nodes/time/pv. Used by the UCI handler to
  // print `info` lines; unset (the default) means no per-iteration reporting.
  auto SetInfoCallback(std::function<void(const SearchInfo&)> cb) -> void {
    info_cb_ = std::move(cb);
  }

  // Replace the runtime search parameters (backing the UCI spin options). The
  // UCI handler applies its current values before each search.
  auto SetParams(const SearchParams& params) -> void { params_ = params; }
  auto GetParams() const -> const SearchParams& { return params_; }

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
  // Set a fixed per-move budget (soft and hard bounds coincide). Used by the
  // fixed-time harnesses and `--st` play.
  auto SetSearchTime(float t) -> void;
  // Set soft/hard bounds and the neutral base budget for clock-based play;
  // enables difficulty-scaled dynamic time management.
  auto SetTimeBounds(float soft, float hard, float base) -> void;

  // Search with no time bound (UCI `go infinite`): runs until a depth/node
  // limit is hit or RequestStop() is called.
  auto SetInfiniteSearch() -> void;
  // Cap iterative deepening at `depth` plies (UCI `go depth`). Pass
  // kSearchLimit to remove the cap.
  auto SetDepthLimit(int depth) -> void;
  // Stop once the search has visited `nodes` nodes (UCI `go nodes`). Pass
  // UINT64_MAX to remove the cap.
  auto SetNodeLimit(uint64_t nodes) -> void;
  // Ask an in-progress search to stop as soon as possible (UCI `stop`).
  // Thread-safe: callable from another thread while GetBestMove() runs.
  auto RequestStop() -> void { stop_requested_.store(true); }

  // Restrict the root search to these moves (UCI `go searchmoves`). Empty (the
  // default) means all legal moves; set fresh before each search.
  auto SetSearchMoves(const vector<Move>& moves) -> void {
    search_moves_ = moves;
  }
  // Stop as soon as a mate in <= `moves` for the side to move is found (UCI
  // `go mate`); 0 disables.
  auto SetMateTarget(int moves) -> void { mate_target_ = moves; }

  // UCI `ponderhit`: the pondered move was played, so give the in-progress
  // (infinite) ponder search a real deadline `soft`/`hard` seconds from now, on
  // top of the time already spent pondering. Thread-safe; call while
  // GetBestMove() runs on the worker.
  auto PonderHit(float soft, float hard) -> void;
  // The move the engine expects the opponent to reply with: the second move of
  // the last search's principal variation, or an empty Move if the PV is shorter
  // than two plies. Read after GetBestMove() returns; used for `bestmove ...
  // ponder ...`.
  auto GetPonderMove() const -> Move {
    return completed_pv_len_ >= 2 ? completed_pv_[1] : Move{};
  }

  auto GetTotalNodes() const -> uint64_t {
    return total_nodes_ + nodes_since_time_check_;
  }

#ifdef BENCHMARK
  auto BenchmarkReport(int search_depth) -> void;
#endif

 private:
  // Queries and predicates (bool).
  auto InEndgame() const -> bool;
  auto IsKillerMove(const Move& move, int ply) const -> bool;
  auto RepDetected() const -> bool;
  // Return if Zugzwang is unlikely, indicating Null-Move Heuristic should be
  // used.
  auto ZugzwangUnlikely() const -> bool;
  auto ValidateTtMove(const Move& move) const -> bool;
  auto ProbeTt(int& alpha, int& beta, int depth, int ply, int& result) -> bool;
  auto ShouldNullMovePrune(int alpha, int beta, int depth, int ply,
                           bool at_pv_node, bool in_check) -> bool;
  auto ShouldReverseFutilityPrune(int static_eval, int depth, int beta,
                                  bool at_pv_node, bool in_check) -> bool;
  auto ShouldFutilityPrune(const Move& move, int static_eval, int depth,
                           bool at_pv_node, bool in_check, int alpha) -> bool;
  auto ShouldLateMovePrune(const Move& move, int num_quiet_searched, int depth,
                           bool at_pv_node, bool gives_check, bool in_check,
                           int ply) -> bool;
  auto ShouldSeePrune(const Move& move, int depth, bool at_pv_node,
                      bool gives_check, bool in_check, int see_val) -> bool;

  // Dynamic time management (used between iterative-deepening iterations).
  // Signed [-1, +1] best-move-stability signal over the last tm_window depths
  // (negative = stable, positive = churning).
  auto MoveStabilitySignal(int depth) const -> double;
  // Signed [-1, +1] score-stability signal over the last tm_window depths
  // (negative = flat/smooth, positive = large swings / oscillation).
  auto ScoreStabilitySignal(int depth) const -> double;
  // Detect whether the root is an obvious forced recapture (opponent just
  // captured; exactly one safe recapture of it; not in check).
  auto DetectObviousRecapture() -> void;
  // Fold the last root search's best-move node share into the EMA.
  auto UpdateSubtreeShare() -> void;
  // Signed [-1, +1] subtree/node-effort signal (negative = one move dominates
  // the search, positive = effort spread across root moves).
  auto SubtreeStabilitySignal() const -> double;
  // Difficulty multiplier applied to base_time_ to get the soft bound.
  auto ComputeDifficulty(int depth) const -> double;
  // Whether the next iteration is predicted to exceed the soft bound (and so
  // shouldn't be started).
  auto PredictNextIterExceeds(int depth) const -> bool;

  // Search and scoring (int).
  // Computes best evaluation resulting from a legal move for the moving
  // player by searching the tree of possible moves using the Negamax
  // algorithm.
  auto AspirationSearch(int prev_score, int depth, int ply, Move& best_move)
      -> int;
  auto Pvs(int alpha, int beta, int depth, int ply, bool null_move_allowed)
      -> int;
  auto Pvs(Move& pv_move, int alpha, int beta, int depth, int ply,
           bool null_move_allowed) -> int;
  // Search until a "quiescent" position is reached (no capturing moves can be
  // made) to mitigate the horizon effect.
  auto QuiescenceSearch(int alpha, int beta, int ply, int qs_depth = 20) -> int;
  auto GetCorrectedEval(int static_eval) const -> int;
  auto ComputeLmrReduction(int depth, int legal_moves, S8 player_to_move,
                           const Move& move) -> int;
  auto TrySingularExtension(const TableEntry& hash_entry, int depth, int ply,
                            int beta) -> int;

  // Move ordering (vector<Move>).
  // Attempts to predict which moves are likely to be better, and order those
  // towards the front of the move_list to increase the number of moves that
  // can be pruned during alpha-beta pruning.
  auto OrderMoves(const vector<Move>& move_list, int ply) const -> vector<Move>;
  auto OrderMoves(const vector<Move>& move_list) const -> vector<Move>;

  // Move generation and state updates (void).
  auto AddCastlingMoves(vector<Move>& move_list) const -> void;
  auto AddEpMoves(vector<Move>& move_list, S8 moving_player,
                  S8 other_player) const -> void;
  auto AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                        S8 enemy_player, S8 moving_player, S8 moving_piece,
                        S8 start_sq) const -> void;
  auto CheckSearchTime() -> void;
  auto RecordKillerMove(const Move& move, int ply) -> void;
  auto UpdateHistoryHeuristic(const Move& move, int bonus) -> void;
  auto UpdateContinuationHistory(const Move& prev_move, const Move& move,
                                 int bonus) -> void;
  auto UpdateCorrectionHistory(int static_eval, int search_score, int depth)
      -> void;
  auto UpdateCaptureHistory(const Move& move, int bonus) -> void;
  auto RecordBetaCutoff(const Move& move, int depth, int ply,
                        const vector<Move>& searched_quiet_moves,
                        const vector<Move>& searched_captures) -> void;
  auto StoreTtEntry(int best_eval, int orig_alpha, int beta, int depth, int ply,
                    const Move& best_move) -> void;

  // Track if a board evaluation is improving as a line is being seared.
  bool improving_;

  // Runtime-tunable search parameters (UCI options); defaults reproduce the old
  // constexpr behavior.
  SearchParams params_;

  Board* board_;

  // Stop starting new iterative-deepening iterations past `soft_time_`; abort a
  // search in progress once `hard_time_` is reached. `base_time_` is the neutral
  // budget the soft bound is rescaled from by search difficulty. In fixed-time
  // modes the three coincide and dynamic scaling is disabled. soft_/hard_time_
  // are atomic so PonderHit() can revise the running search's deadline from the
  // UCI thread; access is via atomic<float>'s implicit load/store conversions.
  std::atomic<float> soft_time_;
  std::atomic<float> hard_time_;
  float base_time_;
  bool dynamic_tm_;

  // Non-time search limits. `depth_limit_` caps iterative deepening;
  // `node_limit_` caps visited nodes; `stop_requested_` is set from another
  // thread to abort a running search. All checked via the OutOfTime abort path.
  int depth_limit_;
  uint64_t node_limit_;
  std::atomic<bool> stop_requested_;

  // Root-move restriction for UCI `go searchmoves` (empty = all legal moves) and
  // the UCI `go mate` target in moves (0 = disabled). Both set per search.
  vector<Move> search_moves_;
  int mate_target_;

  // Per-iteration signal state for dynamic time management (reset each search).
  // `root_best_history_[d]` is the root best move after completing depth d;
  // `iter_elapsed_[d]` is the cumulative search time at the end of depth d.
  Move root_best_history_[kSearchLimit + 1];
  int root_score_history_[kSearchLimit + 1];
  float iter_elapsed_[kSearchLimit + 1];

  // Triangular principal-variation table: `pv_table_[ply]` holds the PV starting
  // at that ply (best move first), `pv_length_[ply]` its length. Collected only
  // at PV nodes in Pvs; `pv_table_[0]`/`pv_length_[0]` is the root PV. Rows are
  // over-sized by one so a child at `ply + 1` is always addressable.
  Move pv_table_[kSearchLimit + 1][kSearchLimit];
  int pv_length_[kSearchLimit + 1];
  // Snapshot of the root PV after the last *completed* depth. pv_table_[0] is
  // wiped by the entry reset of a subsequent (possibly aborted) iteration, so
  // the ponder move and any post-search PV read must come from here.
  Move completed_pv_[kSearchLimit];
  int completed_pv_len_;
  // Optional per-iteration reporting hook (see SetInfoCallback). Empty if unset.
  std::function<void(const SearchInfo&)> info_cb_;

  // Per-root-move node counts for the last root search, its move count, and the
  // index of the best root move; plus the EMA-smoothed best-move node share.
  uint64_t root_move_nodes_[kMaxRootMoves];
  int root_move_count_;
  int best_root_idx_;
  double subtree_share_ema_;
  bool subtree_ema_init_;

  // The single safe recapture of the opponent's last capture, if the root is an
  // obvious-recapture position (set once per search).
  Move obvious_recapture_;
  bool has_obvious_recapture_;

  high_resolution_clock::time_point search_start_;

  int nodes_since_time_check_;
  int history_heuristic_[kNumPlayers][kNumPieceTypes][kNumSq];
  int continuation_history_[kNumPieceTypes][kNumSq][kNumPieceTypes][kNumSq];
  int eval_history_[kSearchLimit];
  int correction_history_[kNumPlayers][kCorrHistSize];
  int capture_history_[kNumPlayers][kNumPieceTypes][kNumSq][kNumPieceTypes];

  uint64_t total_nodes_;

  Move countermove_table_[kNumPieceTypes][kNumSq];
  Move excluded_move_;

  pair<Move, Move> killer_moves_[kSearchLimit];

  vector<U64> pos_history_;

  S8 user_side_;

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
  soft_time_ = t;
  hard_time_ = t;
  base_time_ = t;
  dynamic_tm_ = false;
  depth_limit_ = kSearchLimit;
  node_limit_ = UINT64_MAX;
  stop_requested_.store(false);
}

inline auto Engine::SetTimeBounds(float soft, float hard, float base) -> void {
  constexpr float kMinSearchTime = 0.01f;
  soft_time_ = std::max(soft, kMinSearchTime);
  hard_time_ = std::max(hard, kMinSearchTime);
  base_time_ = std::max(base, kMinSearchTime);
  // v4: gated off (static soft/hard). Flip kDynamicTmEnabled for v5 tuning.
  dynamic_tm_ = kDynamicTmEnabled;
  depth_limit_ = kSearchLimit;
  node_limit_ = UINT64_MAX;
  stop_requested_.store(false);
}

inline auto Engine::PonderHit(float soft, float hard) -> void {
  // Rebase the deadline onto the wall clock: the ponder search has already run
  // for `elapsed` seconds since search_start_, so allow `hard`/`soft` more from
  // now by comparing against elapsed + budget (CheckSearchTime and the ID loop
  // measure time from search_start_). search_start_ is set once by the worker
  // before any ponderhit can arrive, so reading it here is race-free.
  float elapsed = duration_cast<duration<float>>(high_resolution_clock::now() -
                                                 search_start_)
                      .count();
  soft_time_.store(elapsed + std::max(soft, 0.01f));
  hard_time_.store(elapsed + std::max(hard, 0.01f));
}

inline auto Engine::SetInfiniteSearch() -> void {
  // A far-future deadline the wall clock never reaches, so only a depth/node
  // limit or RequestStop() ends the search.
  soft_time_ = std::numeric_limits<float>::max();
  hard_time_ = std::numeric_limits<float>::max();
  base_time_ = std::numeric_limits<float>::max();
  dynamic_tm_ = false;
  depth_limit_ = kSearchLimit;
  node_limit_ = UINT64_MAX;
  stop_requested_.store(false);
}

inline auto Engine::SetDepthLimit(int depth) -> void {
  depth_limit_ = std::clamp(depth, 1, kSearchLimit);
}

inline auto Engine::SetNodeLimit(uint64_t nodes) -> void {
  node_limit_ = nodes;
}

// Implement private inline member functions.

// --- Queries and predicates (bool) ---

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
    if (pos_history_[pos_idx] == current) {
      return true;
    };
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

inline auto Engine::ValidateTtMove(const Move& move) const -> bool {
  if (move.IsEmpty()) {
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

inline auto Engine::ShouldReverseFutilityPrune(int static_eval, int depth,
                                               int beta, bool at_pv_node,
                                               bool in_check) -> bool {
  if (depth > 2 || at_pv_node || in_check) {
    return false;
  }
  if (improving_) {
    // Prune less aggressively when the line's static evaluations are improving.
    return static_eval - (depth - 1) * params_.futility_margin >= beta;
  }
  return static_eval - depth * params_.futility_margin >= beta;
}

inline auto Engine::ShouldFutilityPrune(const Move& move, int static_eval,
                                        int depth, bool at_pv_node,
                                        bool in_check, int alpha) -> bool {
  return depth <= params_.max_futility_pruning_depth && !at_pv_node && !in_check &&
         move.captured_piece == kNA && move.promoted_to_piece == kNA &&
         static_eval + depth * params_.futility_margin <= alpha;
}

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
  return !at_pv_node && depth <= params_.max_late_move_pruning_depth &&
         num_quiet_searched > lmpThreshold && move.captured_piece == kNA &&
         move.promoted_to_piece == kNA && !gives_check && !in_check &&
         !IsKillerMove(move, ply);
}

inline auto Engine::ShouldSeePrune(const Move& move, int depth, bool at_pv_node,
                                   bool gives_check, bool in_check, int see_val)
    -> bool {
  if (at_pv_node || depth > params_.max_see_pruning_depth || move.captured_piece == kNA ||
      gives_check || in_check) {
    return false;
  }
  return see_val < -depth * params_.see_margin;
}

// --- Search and scoring (int) ---

inline auto Engine::Pvs(int alpha, int beta, int depth, int ply,
                        bool null_move_allowed) -> int {
  Move pv_move;
  return Pvs(pv_move, alpha, beta, depth, ply, null_move_allowed);
}

inline auto Engine::GetCorrectedEval(int static_eval) const -> int {
  S8 player = board_->GetPlayerToMove();
  int idx = board_->GetPawnHash() % kCorrHistSize;
  int correction = correction_history_[player][idx];
  return static_eval + correction / kCorrHistGrain;
}

inline auto Engine::ComputeLmrReduction(int depth, int legal_moves,
                                        S8 player_to_move, const Move& move)
    -> int {
  int reduction = static_cast<int>(sqrt(static_cast<double>(depth - 1)) +
                                   sqrt(static_cast<double>(legal_moves - 1)));
  int history_score =
      history_heuristic_[player_to_move][move.moving_piece][move.target_sq];
  if (history_score > 0) {
    --reduction;
  } else if (history_score < params_.history_lmr_threshold) {
    ++reduction;
  }
  if (!improving_) {
    // Reduce depth more if the line's evaluations aren't improving.
    ++reduction;
  }
  return max(1, reduction);
}

// --- Move generation and state updates (void) ---

inline auto Engine::CheckSearchTime() -> void {
  if (++nodes_since_time_check_ < 4096) {
    return;
  }

  total_nodes_ += 4096;
  nodes_since_time_check_ = 0;
  // An external stop (UCI `stop`/`quit`) or a node budget aborts immediately;
  // both are checked here (not per node) so the hot path stays branch-light.
  if (stop_requested_.load(std::memory_order_relaxed) ||
      total_nodes_ >= node_limit_) {
    throw OutOfTime();
  }
  float time_since_search_started =
      duration_cast<duration<float>>(high_resolution_clock::now() -
                                     search_start_)
          .count();
  if (time_since_search_started >= hard_time_) {
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

// History-gravity saturation bound. Kept compile-time (not runtime-tunable):
// it divides in the per-cutoff history updates below, and as a power-of-two
// constant that division is a shift; a runtime value forces a real integer
// division. See the SearchParams comment.
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

inline auto Engine::UpdateContinuationHistory(const Move& prev_move,
                                              const Move& move, int bonus)
    -> void {
  bonus = clamp(bonus, -kMaxHistoryBonus, kMaxHistoryBonus);
  int& cont_history =
      continuation_history_[prev_move.moving_piece][prev_move.target_sq]
                           [move.moving_piece][move.target_sq];
  // Update the continuation history value using the history gravity formula.
  cont_history += (bonus - cont_history * abs(bonus) / kMaxHistoryBonus);
}

inline auto Engine::UpdateCorrectionHistory(int static_eval, int search_score,
                                            int depth) -> void {
  S8 player = board_->GetPlayerToMove();
  int idx = board_->GetPawnHash() % kCorrHistSize;
  int diff = search_score - static_eval;
  int weight = min(depth + 1, 16);
  int& entry = correction_history_[player][idx];
  entry += (diff * kCorrHistGrain - entry) * weight / 256;
  entry = clamp(entry, -kCorrHistMax * kCorrHistGrain,
                kCorrHistMax * kCorrHistGrain);
}

inline auto Engine::UpdateCaptureHistory(const Move& move, int bonus) -> void {
  S8 player_to_move = board_->GetPlayerToMove();
  bonus = clamp(bonus, -kMaxHistoryBonus, kMaxHistoryBonus);
  if (move.captured_piece == kNA) {
    throw invalid_argument(
        "move.captured_piece in Engine::UpdateCaptureHistory()");
  }
  int& history = capture_history_[player_to_move][move.moving_piece]
                                 [move.target_sq][move.captured_piece];
  // Update the history heuristic value using the history gravity formula.
  history += (bonus - history * abs(bonus) / kMaxHistoryBonus);
}

inline auto Engine::StoreTtEntry(int best_eval, int orig_alpha, int beta,
                                 int depth, int ply, const Move& best_move)
    -> void {
  // The node type is decided on the root-relative score, but a mate score is
  // stored rebased to this node so it stays valid at other root distances.
  int tt_eval = ScoreToTt(best_eval, ply);
  if (best_eval <= orig_alpha) {
    transposition_table_.Update(board_, depth, tt_eval, kAllNode);
  } else if (best_eval >= beta) {
    transposition_table_.Update(board_, depth, tt_eval, kCutNode, best_move);
  } else {
    transposition_table_.Update(board_, depth, tt_eval, kPvNode, best_move);
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
