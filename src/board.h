// Noah Himed
// 18 March 2021
//
// Define a Board object, which includes both bitboard and 8x8 board
// representations which store piece locations.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#ifndef OMEGAZERO_SRC_BOARD_H_
#define OMEGAZERO_SRC_BOARD_H_

#include "constants.h"
#include "move.h"

#include <unordered_map>

class Board {
public:
  Board();
  Bitboard GetAttackMask(Player attacking_player, Piece attacking_piece,
                         Square square) const;
  int GetPieceOnSquare(int rank, int file) const;
  void UpdateBoard(Move move);
private:
  Bitboard GetPiecesByType(Piece piece_type, int player = kNA) const;

  // Store an array of one bitboard per piece type and one bitboard per
  // player containing all active pieces from their side.
  Bitboard pieces_[kNumBitboards];
  // Store an 8x8 board representation.
  int layout_[kNumRanks][kNumFiles];
  int bishop_magic_lengths_[kNumRanks][kNumFiles];
  int rook_magic_lengths_[kNumRanks][kNumFiles];

  std::unordered_map<char, char> hex_map_;
};

extern const Bitboard kNonSliderAttackMasks[kNumNonSliderMasks][kNumSquares];
// Store all positions sliding pieces can move to on an empty board,
// excluding endpoints.
extern const Bitboard kSliderPieceMasks[kNumSliderMasks][kNumSquares];
// Store masks of the endpoints of rays in slider piece occupancy masks.
extern const Bitboard kSliderEndpointMasks[kNumSliderMasks][kNumSquares];
extern const Bitboard kMagics[kNumSliderMasks][kNumSquares];
extern const std::unordered_map<const char*, Bitboard> kMagicIndexToAttackMap;

#endif // OMEGAZERO_SRC_BOARD_H_
