/* Noah Himed
 *
 * Define the tunable-search-parameter registry and the params.json load/store
 * layer. The registry is the single source of truth mapping each UCI spin
 * option / SPSA target / JSON key onto a SearchParams field, shared by the UCI
 * handler (option advertisement + setoption) and the JSON (de)serializer here.
 *
 * params.json (at the repo root) holds two profiles keyed by eval mode: "nnue"
 * and "hce". Values are stored in the same integer units the UCI options use
 * (doubles are scaled by their `divisor`, e.g. a weight of 0.50 is stored 50),
 * so JSON, `setoption`, and SPSA all speak identical units.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_PARAMS_H_
#define OMEGAZERO_SRC_PARAMS_H_

#include <string>
#include <utility>
#include <vector>

#include "engine.h"

namespace omegazero {

// One integer-valued tunable parameter: its UCI/JSON name, the SearchParams
// field it maps to, and the [min, max] bounds (also the GUI-advertised range).
struct IntOpt {
  const char* name;
  int SearchParams::* field;
  int def, min, max;
};

// One real-valued tunable parameter. The stored double equals the UCI/JSON
// integer divided by `divisor` (so a weight of 0.50 is surfaced/stored as 50).
struct DblOpt {
  const char* name;
  double SearchParams::* field;
  int divisor;
  int def, min, max;
};

// The registry. `inline constexpr` so both uci.cc and params.cc share one copy.
inline constexpr IntOpt kIntOpts[] = {
    {"TmWindow", &SearchParams::tm_window, 5, 1, 30},
    {"AspirationDelta", &SearchParams::aspiration_delta, 25, 1, 200},
    {"FutilityMargin", &SearchParams::futility_margin, 200, 20, 600},
    {"MaxFutilityPruningDepth", &SearchParams::max_futility_pruning_depth, 2, 0,
     8},
    {"MaxLateMovePruningDepth", &SearchParams::max_late_move_pruning_depth, 2, 0,
     8},
    {"MaxSeePruningDepth", &SearchParams::max_see_pruning_depth, 5, 0, 12},
    {"SeeMargin", &SearchParams::see_margin, 100, 10, 400},
    {"HistoryLmrThreshold", &SearchParams::history_lmr_threshold, -1000, -8000,
     0},
    {"NumEarlyMoves", &SearchParams::num_early_moves, 3, 1, 12},
    {"MinReductionDepth", &SearchParams::min_reduction_depth, 3, 1, 8},
    {"MinIirDepth", &SearchParams::min_iir_depth, 4, 1, 12},
    {"MaxRazoringDepth", &SearchParams::max_razoring_depth, 3, 0, 8},
    {"RazoringMargin", &SearchParams::razoring_margin, 350, 50, 1000},
    {"SingularDepthMin", &SearchParams::singular_depth_min, 6, 3, 16},
    {"NullMoveDepthMin", &SearchParams::null_move_depth_min, 4, 1, 12},
    {"NullMoveDepthHighR", &SearchParams::null_move_depth_high_r, 6, 2, 16},
    {"QsDelta", &SearchParams::qs_delta, 900, 100, 2000},
};

inline constexpr DblOpt kDblOpts[] = {
    {"TmMoveDecay", &SearchParams::tm_move_decay, 100, 60, 0, 100},
    {"TmMoveWeight", &SearchParams::tm_move_weight, 100, 50, 0, 300},
    {"TmScoreWeight", &SearchParams::tm_score_weight, 100, 30, 0, 300},
    {"TmScoreScale", &SearchParams::tm_score_scale, 1, 100, 10, 1000},
    {"TmOscWeight", &SearchParams::tm_osc_weight, 100, 50, 0, 300},
    {"TmMateDifficulty", &SearchParams::tm_mate_difficulty, 100, 50, 1, 300},
    {"TmObviousDifficulty", &SearchParams::tm_obvious_difficulty, 100, 25, 1,
     300},
    {"TmSubtreeWeight", &SearchParams::tm_subtree_weight, 100, 20, 0, 300},
    {"TmSubtreeEmaAlpha", &SearchParams::tm_subtree_ema_alpha, 100, 50, 1, 100},
    {"TmDifficultyMin", &SearchParams::tm_difficulty_min, 100, 45, 1, 100},
    {"TmDifficultyMax", &SearchParams::tm_difficulty_max, 100, 250, 100, 500},
    {"TmEbfMin", &SearchParams::tm_ebf_min, 100, 150, 100, 400},
    {"TmEbfMax", &SearchParams::tm_ebf_max, 100, 400, 100, 800},
    {"TmEbfFallback", &SearchParams::tm_ebf_fallback, 100, 200, 100, 400},
};

// The params.json profile name matching the active eval mode: "nnue" if NNUE
// weights are loaded (g_nnue.IsLoaded()), else "hce".
auto ProfileForEvalMode() -> std::string;

// Overlay the named profile from `path` onto `out`, which the caller seeds with
// defaults. Only recognized keys present in the profile are applied (clamped to
// each option's bounds); everything else keeps its incoming value. Returns true
// iff the file was opened and contained the requested profile object. A missing
// file or profile is not an error: `out` is simply left at its defaults.
auto LoadProfileInto(const std::string& path, const std::string& profile,
                     SearchParams& out) -> bool;

// Serialize the given (name, params) profiles to `path` as pretty JSON in the
// registry's integer units. Used to generate/refresh params.json and to write
// back SPSA-tuned results. Returns false if the file could not be opened.
auto WriteParamsJson(
    const std::string& path,
    const std::vector<std::pair<std::string, SearchParams>>& profiles) -> bool;

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_PARAMS_H_
