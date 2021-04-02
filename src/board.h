// Noah Himed
// 18 March 2021
//
// Define a Board object, which includes both bitboard and 8x8 board
// representations which store piece locations.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#ifndef OMEGAZERO_SRC_BOARD_H_
#define OMEGAZERO_SRC_BOARD_H_

#include <gmp.h>

#include <unordered_map>

#include "constants.h"
#include "move.h"

using namespace std;

class Board {
public:
  Board();
  // Return possible attacks on both players' pieces.
  Bitboard GetAttackMask(int attacking_player, int square,
                         Piece attacking_piece) const;
  Bitboard GetPiecesByType(Piece piece_type, int player = kNA) const;
  int GetPieceOnSquare(int rank, int file) const;
  int GetPlayerOnSquare(int rank, int file) const;
  void UpdateBoard(Move move);
private:
  Bitboard pieces_[kNumBitboards];
  Bitboard player_pieces_[kNumPlayers];

  // Store an 8x8 board representation.
  int piece_layout_[kNumRanks][kNumFiles];
  int player_layout_[kNumRanks][kNumFiles];
  int bishop_magic_lengths_[kNumSquares];
  int rook_magic_lengths_[kNumSquares];

  unordered_map<char, char> hex_map_;
};

const int kInitialPieceLayout[kNumRanks][kNumFiles] = {
  {kRook, kKnight, kBishop, kQueen, kKing, kBishop, kKnight, kRook},
  {kPawn, kPawn, kPawn, kPawn, kPawn, kPawn, kPawn, kPawn},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kPawn, kPawn, kPawn, kPawn, kPawn, kPawn, kPawn, kPawn},
  {kRook, kKnight, kBishop, kQueen, kKing, kBishop, kKnight, kRook}
};
const int kInitialPlayerLayout[kNumRanks][kNumFiles] = {
  {kWhite, kWhite, kWhite, kWhite, kWhite, kWhite, kWhite, kWhite},
  {kWhite, kWhite, kWhite, kWhite, kWhite, kWhite, kWhite, kWhite},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kNA, kNA, kNA, kNA, kNA, kNA, kNA, kNA},
  {kBlack, kBlack, kBlack, kBlack, kBlack, kBlack, kBlack, kBlack},
  {kBlack, kBlack, kBlack, kBlack, kBlack, kBlack, kBlack, kBlack}
};
const int kBishopMagicLengths[kNumSquares] = {
  6, 5, 5, 5, 5, 5, 5, 6,
  5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 7, 7, 7, 7, 5, 5,
  5, 5, 7, 9, 9, 7, 5, 5,
  5, 5, 7, 9, 9, 7, 5, 5,
  5, 5, 7, 7, 7, 7, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5,
  6, 5, 5, 5, 5, 5, 5, 6
};
const int kRookMagicLengths[kNumSquares] = {
  12, 11, 11, 11, 11, 11, 11, 12,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  12, 11, 11, 11, 11, 11, 11, 12
};

extern const Bitboard kNonSliderAttackMasks[kNumNonSliderMasks][kNumSquares];
// Store all positions sliding pieces can move to on an empty board,
// excluding endpoints.
extern const Bitboard kSliderPieceMasks[kNumSliderMasks][kNumSquares];
// Store masks of the endpoints of rays in slider piece occupancy masks.
extern const Bitboard kSliderEndpointMasks[kNumSliderMasks][kNumSquares];
extern const uint64_t kMagics[kNumSliderMasks][kNumSquares];
extern const unordered_map<uint64_t, Bitboard> kMagicIndexToAttackMap;

#endif // OMEGAZERO_SRC_BOARD_H_
