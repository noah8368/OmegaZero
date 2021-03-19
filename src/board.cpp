// Noah Himed
// 18 March 2021
//
// Implement the Board type.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "board.h"
#include "constants.h"

#include <iostream>

Board::Board() {
  // Initialize piece bitboards, indexing from LSB (square 0) to MSB (square 63)
  pieces_[kWhite]  = 0x000000000000FFFF;
  pieces_[kBlack]  = 0xFFFF000000000000;
  pieces_[kPawn]   = 0x00FF00000000FF00;
  pieces_[kKnight] = 0x2400000000000024;
  pieces_[kBishop] = 0x4200000000000042;
  pieces_[kRook]   = 0x1800000000000018;
  pieces_[kQueen]  = 0x0100000000000001;
  pieces_[kKing]   = 0x8000000000000080;

  // Initialize the 8x8 board representation
  for (int rank = k1; rank <= k8; ++rank) {
    for (int file = kA; file <= kH; ++file) {
      layout_[rank][file] = kStartLayout[rank][file];
    }
  }
}

int Board::getPieceOnSquare(int rank, int file) const {
  return layout_[rank][file];
}
