// Noah Himed
// 18 March 2021
//
// Define a Board object, which includes both a bitboard and 8x8 board
// representation which stores piece locations.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#ifndef OMEGAZERO_SRC_BOARD_H_
#define OMEGAZERO_SRC_BOARD_H_

#include "constants.h"

class Board {
public:
  Board();
  Bitboard getOccupiedSpaces(Player player) const;
  Bitboard getEmptySpaces(Player player) const;
  Bitboard getPawns(Player player) const;
  Bitboard getKnights(Player player) const;
  Bitboard getBishops(Player player) const;
  Bitboard getRooks(Player player) const;
  Bitboard getQueen(Player player) const;
  Bitboard getKing(Player player) const;
  int getPieceOnSquare(int rank, int file) const;
private:
  // Store an array of one bitboard per piece type (6) and one bitboard per
  // player with all of their active pieces
  Bitboard pieces_[kNumBitboards];
  int layout_[kNumRanks][kNumFiles];
};

#endif // OMEGAZERO_SRC_BOARD_H_
