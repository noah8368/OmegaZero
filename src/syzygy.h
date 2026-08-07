/* Noah Himed
 *
 * Define a thin adapter over the vendored Fathom Syzygy tablebase prober. A
 * single global instance (g_syzygy) mirrors g_nnue: load once on the main
 * thread, then probe read-only (thread-safe) from every Lazy-SMP search worker.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_SYZYGY_H_
#define OMEGAZERO_SRC_SYZYGY_H_

#include <string>

#include "board.h"

namespace omegazero {

// Win/Draw/Loss result of a WDL probe, from the side-to-move's perspective.
// kCursedWin/kBlessedLoss are wins/losses that the 50-move rule draws.
enum class TbWdl { kFailed, kLoss, kBlessedLoss, kDraw, kCursedWin, kWin };

class Syzygy {
 public:
  // Load tablebases from `path` (Fathom's path syntax). Returns whether any
  // tables were found; MaxPieces() becomes TB_LARGEST (0 if none loaded).
  auto Init(const std::string& path) -> bool;
  auto Free() -> void;
  auto IsLoaded() const -> bool { return max_pieces_ > 0; }
  auto MaxPieces() const -> int { return max_pieces_; }

  // Total number of pieces on `board` (used to gate probe eligibility).
  auto PieceCount(const Board& board) const -> int;
  // Probe the WDL tables. Returns kFailed if the position isn't covered or the
  // probe's preconditions (see Engine::ShouldProbeTb) aren't met.
  auto ProbeWdl(const Board& board) const -> TbWdl;

 private:
  int max_pieces_ = 0;
};

extern Syzygy g_syzygy;

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_SYZYGY_H_
