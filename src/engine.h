/* Noah Himed
 *
 * Define the Engine type: pseudo-legal move generator, search, and evaluation.
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

using std::clamp;
using std::invalid_argument;
using std::max;
using std::min;
using std::numeric_limits;
using std::pair;
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
// Correction-history fixed-point grain and saturation bound. Compile-time
// power-of-two so the per-node divide in GetCorrectedEval is a shift (~8% NPS).
constexpr int kCorrHistGrain = 256;
constexpr int kCorrHistMax = 256;

constexpr int kBestEval = 32000;
constexpr int kNeutralEval = -25;
constexpr int kWorstEval = -32000;
constexpr int kInvalidEval = 32001;

// Syzygy tablebase win/loss scores occupy the band just below the mate band, so
// a forced mate is still preferred over a TB win. A TB win found `ply` plies
// from the root scores kTbWin - ply (shorter wins rank higher); a loss negates.
constexpr int kTbWin = kBestEval - kSearchLimit;

// Mate scores are encoded root-relative as kBestEval minus plies-to-mate (or
// kWorstEval plus plies-to-being-mated), so any score within kSearchLimit of an
// extreme is a mate score.
inline auto IsMateScore(int score) -> bool {
  return score > kBestEval - kSearchLimit || score < kWorstEval + kSearchLimit;
}
// Whether `score` is a Syzygy win/loss score: the kSearchLimit-wide band just
// below the mate band on each side. Provably disjoint from both mate scores and
// normal evals (which never approach the extremes).
inline auto IsTbScore(int score) -> bool {
  return (score > kBestEval - 2 * kSearchLimit && score <= kTbWin) ||
         (score < kWorstEval + 2 * kSearchLimit && score >= -kTbWin);
}
// Rebase a mate or TB score between root-relative and node-relative (at `ply`)
// so it stays valid across TT hits at different root distances. ScoreToTt
// stores, ScoreFromTt retrieves; ordinary scores pass through unchanged.
inline auto ScoreToTt(int score, int ply) -> int {
  if (score > kBestEval - kSearchLimit) return score + ply;
  if (score < kWorstEval + kSearchLimit) return score - ply;
  if (IsTbScore(score)) return score > 0 ? score + ply : score - ply;
  return score;
}
inline auto ScoreFromTt(int score, int ply) -> int {
  if (score > kBestEval - kSearchLimit) return score - ply;
  if (score < kWorstEval + kSearchLimit) return score + ply;
  if (IsTbScore(score)) return score > 0 ? score - ply : score + ply;
  return score;
}

// Runtime search-tuning parameters. There are deliberately NO default values
// here: every field is loaded at startup from params.json (via the registry in
// params.h, see LoadParamsOrDie). Fields are zero-initialized only so a
// default-constructed SearchParams is deterministic; a valid params.json is
// required before the engine may search.
struct SearchParams {
  // --- Dynamic time management ---
  int tm_window{};             // recent iterations weighed for move stability
  double tm_move_decay{};      // geometric decay favoring recent changes
  double tm_move_weight{};     // weight of the best-move-stability term
  double tm_score_weight{};    // weight of the score-stability term
  double tm_score_scale{};     // cp swing mapping to full magnitude-instability
  double tm_osc_weight{};      // extra weight for score oscillation
  double tm_mate_difficulty{};      // difficulty once a mate is found/faced
  double tm_obvious_difficulty{};   // difficulty for an obvious recapture
  double tm_subtree_weight{};  // weight of the subtree/node-effort term
  double tm_subtree_ema_alpha{};    // EMA smoothing of the best-move node share
  double tm_difficulty_min{};  // clamp floor on the difficulty multiplier
  double tm_difficulty_max{};  // clamp ceiling on the difficulty multiplier
  double tm_ebf_min{};         // predictive early-stop EBF clamp floor
  double tm_ebf_max{};         // predictive early-stop EBF clamp ceiling
  double tm_ebf_fallback{};    // EBF used before two iterations complete
  // --- Pruning / reduction margins, depths, thresholds ---
  int aspiration_delta{};  // initial aspiration half-window (cp)
  int futility_margin{};   // per-depth (reverse) futility margin (cp)
  int max_futility_pruning_depth{};  // max depth for (reverse) futility pruning
  int max_late_move_pruning_depth{};  // max depth for late-move pruning
  int max_see_pruning_depth{};        // max depth for SEE pruning
  int see_margin{};                   // per-depth SEE-pruning margin (cp)
  int history_lmr_threshold{};     // history below which LMR reduces one more
  int num_early_moves{};           // moves searched at full depth before LMR
  int min_reduction_depth{};       // min depth for late-move reductions
  int min_iir_depth{};             // min depth for internal iterative reduction
  int max_razoring_depth{};        // max depth for razoring
  int razoring_margin{};           // razoring drop-to-qsearch margin (cp)
  int singular_depth_min{};        // min depth for singular extensions
  int null_move_depth_min{};       // min depth for null-move pruning
  int null_move_depth_high_r{};    // depth above which NMP uses the larger R
  int qs_delta{};                  // quiescence delta-pruning margin (cp)
};

// Upper bound on legal moves (~218), sizing the per-root-move node table.
constexpr int kMaxRootMoves = 256;

// One iterative-deepening iteration's result, passed to the info callback so a
// UCI front-end can emit an `info` line. The engine itself performs no I/O.
struct SearchInfo {
  int depth;
  int score;  // Internal score: centipawns, or a mate score (IsMateScore).
  uint64_t nodes;
  long long time_ms;
  const Move* pv;  // Principal variation, pv[0] first; length is pv_len.
  int pv_len;
};

class Engine {
 public:
  Engine(TranspositionTable* tt, Board* board, S8 player_side,
         float search_time);
  Engine(TranspositionTable* tt, Board* board, S8 player_side, float search_time,
         const vector<U64>& pos_history);

  // Register a callback invoked once per completed depth during GetBestMove().
  // Used by the UCI handler for `info` lines; unset means no reporting.
  auto SetInfoCallback(std::function<void(const SearchInfo&)> cb) -> void {
    info_cb_ = std::move(cb);
  }

  // Runtime search parameters backing the UCI spin options.
  auto SetParams(const SearchParams& params) -> void { params_ = params; }
  auto GetParams() const -> const SearchParams& { return params_; }

  // Find the best legal move via iterative-deepening Negamax search.
  auto GetBestMove() -> Move;
  auto GetBestMove(int& node_score) -> Move;

  // Check for draws, checks, and checkmates (does not detect repetitions).
  auto GetGameStatus() -> S8;
  auto GetUserSide() const -> S8;

  // Whether a Syzygy tablebase probe (root DTZ or in-search WDL) has actually
  // returned a usable result since the last ResetSyzygyUsed(). Lets callers note
  // in the PGN whether the tablebases influenced any move of the game.
  auto SyzygyUsed() const -> bool;
  auto ResetSyzygyUsed() -> void;

  // Count leaves at `depth` plies below the current board state.
  auto Perft(int depth) -> U64;

  // All pseudo-legal moves at the current board state.
  auto GenerateMoves(bool captures_only = false) const -> vector<Move>;

  // Record the current position for repetition detection and return how many
  // times it has been seen.
  auto AddPosToHistory() -> void;
  // Drop the most recently recorded position (used when undoing a move).
  auto PopPosFromHistory() -> void;
  auto ClearHistory() -> void;
  // Position history for repetition detection; used to seed Lazy-SMP helper
  // engines with the same history as the main search.
  auto GetPosHistory() const -> const vector<U64>& { return pos_history_; }
  // Fixed per-move budget (soft/hard coincide); for fixed-time harnesses &
  // `--st`.
  auto SetSearchTime(float t) -> void;
  // Soft/hard bounds + neutral base budget for clock play; enables dynamic TM.
  auto SetTimeBounds(float soft, float hard, float base) -> void;

  // No time bound (UCI `go infinite`): runs until a depth/node limit or stop.
  auto SetInfiniteSearch() -> void;
  // Cap depth (UCI `go depth`); kSearchLimit removes the cap.
  auto SetDepthLimit(int depth) -> void;
  // Cap visited nodes (UCI `go nodes`); UINT64_MAX removes the cap.
  auto SetNodeLimit(uint64_t nodes) -> void;
  // Ask a running search to stop ASAP (UCI `stop`). Thread-safe.
  auto RequestStop() -> void { stop_requested_.store(true); }

  // Restrict the root to these moves (UCI `go searchmoves`); empty = all legal.
  auto SetSearchMoves(const vector<Move>& moves) -> void {
    search_moves_ = moves;
  }
  // Stop once a mate in <= `moves` is found (UCI `go mate`); 0 disables.
  auto SetMateTarget(int moves) -> void { mate_target_ = moves; }

  // UCI `ponderhit`: give the running ponder search a real deadline
  // `soft`/`hard` seconds from now, on top of time already spent pondering.
  // Thread-safe.
  auto PonderHit(float soft, float hard) -> void;
  // Move the engine expects in reply (2nd move of the last PV, or empty if the
  // PV is shorter than two plies). For `bestmove ... ponder ...`.
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
  // Whether Zugzwang is unlikely (i.e. null-move pruning is safe to apply).
  auto ZugzwangUnlikely() const -> bool;
  auto ValidateTtMove(const Move& move) const -> bool;
  auto ProbeTt(int& alpha, int& beta, int depth, int ply, int& result) -> bool;
  // Common Syzygy probe eligibility: tables loaded, few enough pieces, and no
  // castling rights (Syzygy positions have none). No-op unless SyzygyPath/
  // --syzygy loaded tables.
  auto TbPositionEligible() const -> bool;
  // Whether a Syzygy WDL probe is valid: eligible and rule50 == 0 (Fathom's WDL
  // probe requires a zeroed 50-move counter).
  auto ShouldProbeTb() const -> bool;
  // If the root is a tablebase position, decode the DTZ-optimal move, match it
  // to a `root_moves` entry (so it carries the full Move fields), set
  // `score_out` to a TB score, and return it in `tb_move`. Root DTZ probing is
  // valid at any rule50 (it handles the 50-move rule).
  auto ProbeTbRoot(const vector<Move>& root_moves, Move& tb_move, int& score_out)
      -> bool;
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

  // Dynamic time management (between iterative-deepening iterations). Each
  // *StabilitySignal returns a signed [-1, +1] value over the last tm_window
  // depths (negative = stable, positive = churning/unstable).
  auto MoveStabilitySignal(int depth) const -> double;
  auto ScoreStabilitySignal(int depth) const -> double;
  // Detect an obvious forced recapture at the root (opponent just captured;
  // exactly one safe recapture; not in check).
  auto DetectObviousRecapture() -> void;
  // Fold the last root search's best-move node share into the EMA.
  auto UpdateSubtreeShare() -> void;
  // Signed [-1, +1] node-effort signal (negative = one move dominates).
  auto SubtreeStabilitySignal() const -> double;
  // Difficulty multiplier applied to base_time_ to get the soft bound.
  auto ComputeDifficulty(int depth) const -> double;
  // Whether the next iteration is predicted to exceed the soft bound.
  auto PredictNextIterExceeds(int depth) const -> bool;
  // Reset per-search time-management state (obvious-recapture detection and the
  // best-move node-share EMA) before the iterative-deepening loop.
  auto InitTimeManagement() -> void;
  // After a completed iteration at `depth` (with `elapsed` seconds spent so
  // far), rescale the soft bound by search difficulty and report whether the
  // loop should stop (soft bound crossed, or next iteration unlikely to finish).
  auto UpdateSoftBoundAndShouldStop(int depth, float elapsed) -> bool;
  // First legal move among `moves` (honoring `go searchmoves`), or an empty
  // Move if none is legal. Used to guarantee a non-empty best move when the
  // search is aborted before it assigns one.
  auto FirstLegalFallbackMove(const std::vector<Move>& moves) -> Move;

  // Search and scoring (int).
  // Negamax search over the move tree for the moving player's best evaluation.
  auto AspirationSearch(int prev_score, int depth, int ply, Move& best_move)
      -> int;
  auto Pvs(int alpha, int beta, int depth, int ply, bool null_move_allowed)
      -> int;
  auto Pvs(Move& pv_move, int alpha, int beta, int depth, int ply,
           bool null_move_allowed) -> int;
  // Search captures until quiescent to mitigate the horizon effect.
  auto QuiescenceSearch(int alpha, int beta, int ply, int qs_depth = 20) -> int;
  auto GetCorrectedEval(int static_eval) const -> int;
  auto ComputeLmrReduction(int depth, int legal_moves, S8 player_to_move,
                           const Move& move) -> int;
  auto TrySingularExtension(const TableEntry& hash_entry, int depth, int ply,
                            int beta) -> int;

  // Move ordering (vector<Move>).
  // Order likely-best moves first to maximize alpha-beta cutoffs.
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

  // Whether static eval is trending up along the current line.
  bool improving_;

  // Runtime-tunable search parameters (UCI options).
  SearchParams params_;

  Board* board_;

  // Time bounds: stop starting iterations past `soft_time_`, abort past
  // `hard_time_`; `base_time_` is the neutral budget the soft bound scales
  // from. Fixed-time modes make all three coincide (dynamic_tm_ off).
  // soft_/hard_ are atomic so PonderHit() can revise the deadline from the UCI
  // thread.
  std::atomic<float> soft_time_;
  std::atomic<float> hard_time_;
  float base_time_;
  bool dynamic_tm_;

  // Non-time limits, all checked via the OutOfTime abort path.
  // `stop_requested_` is set from another thread.
  int depth_limit_;
  uint64_t node_limit_;
  std::atomic<bool> stop_requested_;

  // Root-move restriction (UCI `go searchmoves`, empty = all) and mate target
  // (UCI `go mate`, 0 = disabled). Both set per search.
  vector<Move> search_moves_;
  int mate_target_;

  // Per-iteration signal state for dynamic TM (reset each search): root best
  // move, root score, and cumulative elapsed time after each depth.
  Move root_best_history_[kSearchLimit + 1];
  int root_score_history_[kSearchLimit + 1];
  float iter_elapsed_[kSearchLimit + 1];

  // Triangular PV table: `pv_table_[ply]` holds the PV from that ply (best
  // first), `pv_length_[ply]` its length; row 0 is the root PV. Rows are
  // over-sized by one so a child at `ply + 1` is always addressable.
  Move pv_table_[kSearchLimit + 1][kSearchLimit];
  int pv_length_[kSearchLimit + 1];
  // Root PV snapshot after the last *completed* depth (pv_table_[0] gets wiped
  // by a later, possibly aborted, iteration). Source for the ponder/post-search
  // PV.
  Move completed_pv_[kSearchLimit];
  int completed_pv_len_;
  // Optional per-iteration reporting hook (see SetInfoCallback).
  std::function<void(const SearchInfo&)> info_cb_;

  // Last root search's per-move node counts, move count, best-move index, and
  // the EMA-smoothed best-move node share.
  uint64_t root_move_nodes_[kMaxRootMoves];
  int root_move_count_;
  int best_root_idx_;
  double subtree_share_ema_;
  bool subtree_ema_init_;

  // The single safe recapture at an obvious-recapture root (set once per
  // search).
  Move obvious_recapture_;
  bool has_obvious_recapture_;

  high_resolution_clock::time_point search_start_;

  int nodes_since_time_check_;
  // History values are bounded to +/-kMaxHistoryBonus (16384) by the
  // history-gravity update, so int16_t holds them with margin and halves the
  // footprint of the two largest tables (continuation_history_ 576->288 KB,
  // duplicated per Lazy-SMP worker). Arithmetic promotes to int on read.
  // correction_history_ stays int: it clamps to +/-kCorrHistMax*kCorrHistGrain
  // (65536), which overflows S16.
  S16 history_heuristic_[kNumPlayers][kNumPieceTypes][kNumSq];
  S16 continuation_history_[kNumPieceTypes][kNumSq][kNumPieceTypes][kNumSq];
  int eval_history_[kSearchLimit];
  int correction_history_[kNumPlayers][kCorrHistSize];
  S16 capture_history_[kNumPlayers][kNumPieceTypes][kNumSq][kNumPieceTypes];

  uint64_t total_nodes_;

  Move countermove_table_[kNumPieceTypes][kNumSq];
  Move excluded_move_;

  pair<Move, Move> killer_moves_[kSearchLimit];

  vector<U64> pos_history_;

  S8 user_side_;

  // Set true whenever a Syzygy probe returns a usable result during search;
  // surfaced via SyzygyUsed() and cleared by ResetSyzygyUsed().
  bool syzygy_used_ = false;

  // Cache of previously evaluated positions. Not owned: injected at construction
  // and shared across all Engines in a Lazy-SMP SearchPool (single-threaded
  // callers each own their own). Must outlive the Engine.
  TranspositionTable* transposition_table_;
};

// Implement public inline member functions.

inline auto Engine::GetBestMove() -> Move {
  int node_score;
  return GetBestMove(node_score);
}

inline auto Engine::GetUserSide() const -> S8 { return user_side_; }

inline auto Engine::SyzygyUsed() const -> bool { return syzygy_used_; }

inline auto Engine::ResetSyzygyUsed() -> void { syzygy_used_ = false; }

inline auto Engine::AddPosToHistory() -> void {
  pos_history_.push_back(board_->GetBoardHash());
}

inline auto Engine::PopPosFromHistory() -> void {
  if (!pos_history_.empty()) {
    pos_history_.pop_back();
  }
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
  // Difficulty-scaled dynamic time management is always on when a clock is set;
  // fixed-time and infinite modes disable it (they have no soft/hard spread).
  dynamic_tm_ = true;
  depth_limit_ = kSearchLimit;
  node_limit_ = UINT64_MAX;
  stop_requested_.store(false);
}

inline auto Engine::PonderHit(float soft, float hard) -> void {
  // Rebase the deadline onto the wall clock: time is measured from
  // search_start_, so the new budget is elapsed-since-start + soft/hard.
  // search_start_ is set by the worker before any ponderhit can arrive, so
  // reading it is race-free.
  float elapsed = duration_cast<duration<float>>(high_resolution_clock::now() -
                                                 search_start_)
                      .count();
  soft_time_.store(elapsed + std::max(soft, 0.01f));
  hard_time_.store(elapsed + std::max(hard, 0.01f));
}

inline auto Engine::SetInfiniteSearch() -> void {
  // Unreachable deadline; only a depth/node limit or RequestStop() ends search.
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
  // Endgame if there are no queens, or (no rooks and) each side has at most one
  // queen plus at most one minor piece.
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
  // Scan back over same-side-to-move positions.
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
    // Prune less aggressively when static eval is improving.
    return static_eval - (depth - 1) * params_.futility_margin >= beta;
  }
  return static_eval - depth * params_.futility_margin >= beta;
}

inline auto Engine::ShouldFutilityPrune(const Move& move, int static_eval,
                                        int depth, bool at_pv_node,
                                        bool in_check, int alpha) -> bool {
  return depth <= params_.max_futility_pruning_depth && !at_pv_node &&
         !in_check && move.captured_piece == kNA &&
         move.promoted_to_piece == kNA &&
         static_eval + depth * params_.futility_margin <= alpha;
}

inline auto Engine::ShouldLateMovePrune(const Move& move,
                                        int num_quiet_searched, int depth,
                                        bool at_pv_node, bool gives_check,
                                        bool in_check, int ply) -> bool {
  int lmpThreshold = 6 + 2 * depth * depth;
  // Lower the move-count threshold when static eval isn't improving.
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
  if (at_pv_node || depth > params_.max_see_pruning_depth ||
      move.captured_piece == kNA || gives_check || in_check) {
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
    // Reduce more when static eval isn't improving.
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
  // Checked here rather than per node to keep the hot path branch-light.
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

// History-gravity saturation bound. Compile-time power-of-two so the divides in
// the history updates below are shifts, not real integer divisions.
constexpr int kMaxHistoryBonus = 16384;

inline auto Engine::UpdateHistoryHeuristic(const Move& move, int bonus)
    -> void {
  S8 player_to_move = board_->GetPlayerToMove();
  bonus = clamp(bonus, -kMaxHistoryBonus, kMaxHistoryBonus);
  S16& history =
      history_heuristic_[player_to_move][move.moving_piece][move.target_sq];
  // History-gravity update.
  history += (bonus - history * abs(bonus) / kMaxHistoryBonus);
}

inline auto Engine::UpdateContinuationHistory(const Move& prev_move,
                                              const Move& move, int bonus)
    -> void {
  bonus = clamp(bonus, -kMaxHistoryBonus, kMaxHistoryBonus);
  S16& cont_history =
      continuation_history_[prev_move.moving_piece][prev_move.target_sq]
                           [move.moving_piece][move.target_sq];
  // History-gravity update.
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
  S16& history = capture_history_[player_to_move][move.moving_piece]
                                 [move.target_sq][move.captured_piece];
  // History-gravity update.
  history += (bonus - history * abs(bonus) / kMaxHistoryBonus);
}

inline auto Engine::StoreTtEntry(int best_eval, int orig_alpha, int beta,
                                 int depth, int ply, const Move& best_move)
    -> void {
  // Node type uses the root-relative score; the stored eval is rebased to this
  // node so a mate score stays valid at other root distances.
  int tt_eval = ScoreToTt(best_eval, ply);
  if (best_eval <= orig_alpha) {
    transposition_table_->Update(board_, depth, tt_eval, kAllNode);
  } else if (best_eval >= beta) {
    transposition_table_->Update(board_, depth, tt_eval, kCutNode, best_move);
  } else {
    transposition_table_->Update(board_, depth, tt_eval, kPvNode, best_move);
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_ENGINE_H_
