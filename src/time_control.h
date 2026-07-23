/* Noah Himed
 *
 * Define the time-control policy that converts a UCI clock into per-move search
 * bounds. Produces a soft bound (target used to decide whether to start another
 * iterative-deepening iteration) and a hard bound (panic deadline that aborts a
 * search in progress).
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_TIME_CONTROL_H
#define OMEGAZERO_SRC_TIME_CONTROL_H

#include <algorithm>

namespace omegazero {

using std::max;
using std::min;

// Per-move search bounds, in seconds. `base` is the neutral per-move budget the
// engine scales by a difficulty factor to get the soft bound each iteration.
struct TimeBounds {
  float soft;
  float hard;
  float base;
};

// Fraction of the increment banked into each move's baseline budget.
constexpr float kIncrementFraction = 0.5f;
// Assumed moves-to-go for a sudden-death control: baseline uses remaining/this.
constexpr float kSuddenDeathMoves = 20.0f;
// Multiple of the baseline the hard (panic) bound is allowed to reach.
constexpr float kHardBaseMult = 4.0f;
// Time reserve kept back to avoid flagging: max of this and 1% of remaining.
constexpr float kMinReserveMs = 50.0f;

// Smallest value either bound may take, in ms. Low because depth 1 always
// completes regardless of the soft bound.
constexpr float kMinSoftMs = 10.0f;
// Hard ceiling on the clock-derived per-move allocation, in ms. Caps every
// clock-based bound so the engine never spends more than this on a single move,
// however much time is on the clock. Does not apply to an explicit `movetime`
// request, which the caller asked for deliberately.
constexpr float kMaxMoveTimeMs = 10000.0f;
// Time shaved off a fixed `movetime` request to avoid overshooting, in ms.
constexpr float kMoveTimeMargin = 50.0f;

// Compute per-move search bounds from the clock for the side to move.
// `remaining_ms`/`inc_ms` are that side's clock and increment; `movestogo` is 0
// when the time control is not periodic; `movetime` is a fixed per-move
// allocation in ms (0 when unset), which overrides the clock-based heuristic.
inline auto ComputeTimeBounds(float remaining_ms, float inc_ms, int movestogo,
                              int movetime) -> TimeBounds {
  if (movetime > 0) {
    float alloc =
        max(static_cast<float>(movetime) - kMoveTimeMargin, kMinSoftMs);
    float secs = alloc / 1000.0f;
    return {secs, secs, secs};
  }

  if (remaining_ms <= 0.0f) {
    float secs = kMinSoftMs / 1000.0f;
    return {secs, secs, secs};
  }

  // Baseline per-move budget. Sudden death assumes a fixed moves-to-go
  // estimate; a periodic control divides remaining time among the moves left
  // before the next time gain.
  float base_ms;
  if (movestogo > 0) {
    base_ms = remaining_ms / (movestogo + 1) + kIncrementFraction * inc_ms;
  } else {
    base_ms = remaining_ms / kSuddenDeathMoves + kIncrementFraction * inc_ms;
  }

  // Hard bound: a panic ceiling that rarely binds, capped so a reserve always
  // remains to avoid flagging.
  float reserve_ms = max(kMinReserveMs, 0.01f * remaining_ms);
  float hard_ms =
      min(kHardBaseMult * base_ms, remaining_ms / 4.0f + inc_ms);
  hard_ms = min(hard_ms, remaining_ms - reserve_ms);
  hard_ms = max(hard_ms, kMinSoftMs);

  // Neutral soft bound (difficulty 1). The engine rescales this by a per-move
  // difficulty factor each iteration, bounded by the hard bound.
  float soft_ms = std::clamp(base_ms, kMinSoftMs, hard_ms);

  // Apply the per-move ceiling. Cap the hard bound first, then hold the soft and
  // base budgets under it so the difficulty rescaling can never exceed the cap.
  hard_ms = min(hard_ms, kMaxMoveTimeMs);
  soft_ms = min(soft_ms, hard_ms);
  base_ms = min(base_ms, hard_ms);
  return {soft_ms / 1000.0f, hard_ms / 1000.0f, base_ms / 1000.0f};
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_TIME_CONTROL_H
