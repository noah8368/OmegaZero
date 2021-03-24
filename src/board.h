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
#include "move.h"

class Board {
public:
  Board();
  Bitboard GetAttackMap(Piece piece_type, Player player) const;
  Bitboard GetPiecesByType(Piece piece_type, int player = kNA) const;
  int GetPieceOnSquare(int rank, int file) const;
  void UpdateBoard(Move move);
private:
  // Store an array of one bitboard per piece type (6) and one bitboard per
  // player with all of their active pieces.
  Bitboard pieces_[kNumBitboards];
  int layout_[kNumRanks][kNumFiles];
};

// Store pawn capture and push moves are in separate entries.
extern const Bitboard kNonSliderAttackMaps[kNumNonSliderMaps][kNumSquares];
// Store occupancy masks used for magic bitboard move generation. These masks
// include ray endpoints.
extern const Bitboard kSliderOccupancyMasks[kNumSliderMasks][kNumSquares];

#endif // OMEGAZERO_SRC_BOARD_H_
