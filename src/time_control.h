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

// Per-move search bounds, in seconds.
struct TimeBounds {
  float soft;
  float hard;
};

// Fraction of the hard bound at which we stop starting new iterative-deepening
// iterations.
constexpr float kSoftTimeFraction = 0.6f;

// Smallest per-move allocation the hard bound is allowed to take, in ms.
constexpr float kMinAllocMs = 100.0f;
// Smallest value the soft bound may take, in ms. Lower than the hard floor
// because depth 1 always completes regardless.
constexpr float kMinSoftMs = 10.0f;
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
        std::max(static_cast<float>(movetime) - kMoveTimeMargin, kMinAllocMs);
    float secs = alloc / 1000.0f;
    return {secs, secs};
  }

  if (remaining_ms <= 0.0f) {
    float secs = kMinAllocMs / 1000.0f;
    return {secs, secs};
  }

  float alloc;
  if (movestogo > 0) {
    alloc = remaining_ms / (movestogo + 1) + inc_ms * 0.8f;
  } else {
    alloc = std::min(remaining_ms * remaining_ms / 1800000.0f,
                     remaining_ms / 30.0f) +
            inc_ms * 0.8f;
  }

  float max_alloc = remaining_ms / 3.0f;
  alloc = std::min(alloc, max_alloc);

  float hard_ms = std::max(alloc, kMinAllocMs);
  float soft_ms = std::max(kSoftTimeFraction * hard_ms, kMinSoftMs);
  return {soft_ms / 1000.0f, hard_ms / 1000.0f};
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_TIME_CONTROL_H
