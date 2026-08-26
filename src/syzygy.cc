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
  Bitboard white, black, kings, queens, rooks, bishops, knights, pawns;
};

static auto BuildInputs(const Board& board) -> TbInputs {
  Bitboard wk = board.GetPiecesByType(kKing, kWhite);
  Bitboard bk = board.GetPiecesByType(kKing, kBlack);
  Bitboard wq = board.GetPiecesByType(kQueen, kWhite);
  Bitboard bq = board.GetPiecesByType(kQueen, kBlack);
  Bitboard wr = board.GetPiecesByType(kRook, kWhite);
  Bitboard br = board.GetPiecesByType(kRook, kBlack);
  Bitboard wb = board.GetPiecesByType(kBishop, kWhite);
  Bitboard bb = board.GetPiecesByType(kBishop, kBlack);
  Bitboard wn = board.GetPiecesByType(kKnight, kWhite);
  Bitboard bn = board.GetPiecesByType(kKnight, kBlack);
  Bitboard wp = board.GetPiecesByType(kPawn, kWhite);
  Bitboard bp = board.GetPiecesByType(kPawn, kBlack);
  TbInputs tb_input;
  tb_input.kings = wk | bk;
  tb_input.queens = wq | bq;
  tb_input.rooks = wr | br;
  tb_input.bishops = wb | bb;
  tb_input.knights = wn | bn;
  tb_input.pawns = wp | bp;
  tb_input.white = wk | wq | wr | wb | wn | wp;
  tb_input.black = bk | bq | br | bb | bn | bp;
  return tb_input;
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
  TbInputs tb_input = BuildInputs(board);
  return GetNumSetSq(tb_input.white | tb_input.black);
}

auto Syzygy::ProbeWdl(const Board& board) const -> TbWdl {
  TbInputs tb_input = BuildInputs(board);
  S8 ep_sq = board.GetEpTargetSq();
  unsigned ep = (ep_sq == kNA) ? 0u : static_cast<unsigned>(ep_sq);
  // Castling is always passed as 0: the caller (ShouldProbeTb) only probes when
  // no side has castling rights, which Syzygy requires.
  unsigned result = tb_probe_wdl(
      tb_input.white, tb_input.black, tb_input.kings, tb_input.queens,
      tb_input.rooks, tb_input.bishops, tb_input.knights, tb_input.pawns,
      static_cast<unsigned>(board.GetHalfmoveClock()), 0u, ep,
      board.GetPlayerToMove() == kWhite);
  return MapWdl(result);
}

auto Syzygy::ProbeRoot(const Board& board, TbRootMove& out) const -> bool {
  TbInputs tb_input = BuildInputs(board);
  S8 ep_sq = board.GetEpTargetSq();
  unsigned ep = (ep_sq == kNA) ? 0u : static_cast<unsigned>(ep_sq);
  unsigned results[TB_MAX_MOVES];
  // Root DTZ probe: works at any rule50 (it accounts for the 50-move rule);
  // castling is 0 since the caller only probes with no castling rights.
  unsigned res = tb_probe_root(
      tb_input.white, tb_input.black, tb_input.kings, tb_input.queens,
      tb_input.rooks, tb_input.bishops, tb_input.knights, tb_input.pawns,
      static_cast<unsigned>(board.GetHalfmoveClock()), 0u, ep,
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
