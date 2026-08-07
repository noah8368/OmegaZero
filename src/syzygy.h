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

// A tablebase-optimal root move: the DTZ probe's recommended move (which
// converts the win / holds the draw correctly under the 50-move rule), given as
// squares + promotion so the caller can match it to a generated Move.
struct TbRootMove {
  S8 from_sq;
  S8 to_sq;
  S8 promo_piece;  // kNA, or kQueen/kRook/kBishop/kKnight
  TbWdl wdl;
};

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
  // Probe the DTZ tables at the root; on success, fill `out` with the optimal
  // move and return true. Valid at any rule50 (handles the 50-move rule), but
  // still requires no castling rights.
  auto ProbeRoot(const Board& board, TbRootMove& out) const -> bool;

 private:
  int max_pieces_ = 0;
};

extern Syzygy g_syzygy;

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_SYZYGY_H_
