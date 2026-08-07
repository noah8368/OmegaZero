/* Noah Himed
 *
 * Implement the Syzygy adapter over the vendored Fathom prober.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "syzygy.h"

#include "fathom/tbprobe.h"

namespace omegazero {

Syzygy g_syzygy;

// Fathom wants per-color occupancy plus per-type bitboards (both colors), all
// in the same a1=0 LERF layout OmegaZero uses, so no square flipping is needed.
struct TbInputs {
  U64 white, black, kings, queens, rooks, bishops, knights, pawns;
};

static auto BuildInputs(const Board& board) -> TbInputs {
  U64 wk = board.GetPiecesByType(kKing, kWhite);
  U64 bk = board.GetPiecesByType(kKing, kBlack);
  U64 wq = board.GetPiecesByType(kQueen, kWhite);
  U64 bq = board.GetPiecesByType(kQueen, kBlack);
  U64 wr = board.GetPiecesByType(kRook, kWhite);
  U64 br = board.GetPiecesByType(kRook, kBlack);
  U64 wb = board.GetPiecesByType(kBishop, kWhite);
  U64 bb = board.GetPiecesByType(kBishop, kBlack);
  U64 wn = board.GetPiecesByType(kKnight, kWhite);
  U64 bn = board.GetPiecesByType(kKnight, kBlack);
  U64 wp = board.GetPiecesByType(kPawn, kWhite);
  U64 bp = board.GetPiecesByType(kPawn, kBlack);
  TbInputs in;
  in.kings = wk | bk;
  in.queens = wq | bq;
  in.rooks = wr | br;
  in.bishops = wb | bb;
  in.knights = wn | bn;
  in.pawns = wp | bp;
  in.white = wk | wq | wr | wb | wn | wp;
  in.black = bk | bq | br | bb | bn | bp;
  return in;
}

// Map Fathom's 5-valued WDL (TB_LOSS..TB_WIN) to our enum.
static auto MapWdl(unsigned wdl) -> TbWdl {
  switch (wdl) {
    case TB_LOSS:
      return TbWdl::kLoss;
    case TB_BLESSED_LOSS:
      return TbWdl::kBlessedLoss;
    case TB_DRAW:
      return TbWdl::kDraw;
    case TB_CURSED_WIN:
      return TbWdl::kCursedWin;
    case TB_WIN:
      return TbWdl::kWin;
    default:  // TB_RESULT_FAILED
      return TbWdl::kFailed;
  }
}

// Map Fathom's promotion code to a piece type (kNA when not a promotion).
static auto MapPromo(unsigned tb_promo) -> S8 {
  switch (tb_promo) {
    case TB_PROMOTES_QUEEN:
      return kQueen;
    case TB_PROMOTES_ROOK:
      return kRook;
    case TB_PROMOTES_BISHOP:
      return kBishop;
    case TB_PROMOTES_KNIGHT:
      return kKnight;
    default:  // TB_PROMOTES_NONE
      return kNA;
  }
}

auto Syzygy::Init(const std::string& path) -> bool {
  if (path.empty() || !tb_init(path.c_str())) {
    max_pieces_ = 0;
    return false;
  }
  // TB_LARGEST is the largest piece count any loaded table covers (0 if none),
  // so this transparently reports 5-, 6-, or 7-man depending on what's present.
  max_pieces_ = static_cast<int>(TB_LARGEST);
  return IsLoaded();
}

auto Syzygy::Free() -> void {
  if (IsLoaded()) {
    tb_free();
  }
  max_pieces_ = 0;
}

auto Syzygy::PieceCount(const Board& board) const -> int {
  TbInputs in = BuildInputs(board);
  return GetNumSetSq(in.white | in.black);
}

auto Syzygy::ProbeWdl(const Board& board) const -> TbWdl {
  TbInputs in = BuildInputs(board);
  S8 ep_sq = board.GetEpTargetSq();
  unsigned ep = (ep_sq == kNA) ? 0u : static_cast<unsigned>(ep_sq);
  // Castling is always passed as 0: the caller (ShouldProbeTb) only probes when
  // no side has castling rights, which Syzygy requires.
  unsigned result = tb_probe_wdl(
      in.white, in.black, in.kings, in.queens, in.rooks, in.bishops, in.knights,
      in.pawns, static_cast<unsigned>(board.GetHalfmoveClock()), 0u, ep,
      board.GetPlayerToMove() == kWhite);
  return MapWdl(result);
}

auto Syzygy::ProbeRoot(const Board& board, TbRootMove& out) const -> bool {
  TbInputs in = BuildInputs(board);
  S8 ep_sq = board.GetEpTargetSq();
  unsigned ep = (ep_sq == kNA) ? 0u : static_cast<unsigned>(ep_sq);
  unsigned results[TB_MAX_MOVES];
  // Root DTZ probe: works at any rule50 (it accounts for the 50-move rule);
  // castling is 0 since the caller only probes with no castling rights.
  unsigned res = tb_probe_root(
      in.white, in.black, in.kings, in.queens, in.rooks, in.bishops, in.knights,
      in.pawns, static_cast<unsigned>(board.GetHalfmoveClock()), 0u, ep,
      board.GetPlayerToMove() == kWhite, results);
  if (res == TB_RESULT_FAILED) {
    return false;
  }
  out.from_sq = static_cast<S8>(TB_GET_FROM(res));
  out.to_sq = static_cast<S8>(TB_GET_TO(res));
  out.promo_piece = MapPromo(TB_GET_PROMOTES(res));
  out.wdl = MapWdl(TB_GET_WDL(res));
  return true;
}

}  // namespace omegazero
