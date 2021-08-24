/* Noah Himed
*
* Define a Board object, which includes both bitboard and 8x8 board
* representations to store piece locations.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

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
                         int attacking_piece) const;
  Bitboard GetPiecesByType(int piece_type, int player = kNA) const;
  // Return false if the move was found to put the moving player's King in
  // check. Return true is the board state was succesfully updated.
  bool MakeMove (Move move, string& err_msg);
  int GetPieceOnSquare(int rank, int file) const;
  int GetPlayerOnSquare(int rank, int file) const;
private:
  bool KingInCheck() const;

  Bitboard pieces_[kNumBitboards];
  Bitboard player_pieces_[kNumPlayers];
  // Store an 8x8 board representation.
  int piece_layout_[kNumRanks][kNumFiles];
  int player_layout_[kNumRanks][kNumFiles];
  int bishop_magic_lengths_[kNumSquares];
  int rook_magic_lengths_[kNumSquares];
  unordered_map<char, char> hex_map_;
};

const Bitboard kFileMasks[kNumFiles] = {
  0X0101010101010101, 0X0202020202020202,
  0X0404040404040404, 0X0808080808080808,
  0X1010101010101010, 0X2020202020202020,
  0X4040404040404040, 0X8080808080808080
};
const Bitboard kRankMasks[kNumRanks] = {
  0X00000000000000FF, 0X000000000000FF00,
  0X0000000000FF0000, 0X00000000FF000000,
  0X000000FF00000000, 0X0000FF0000000000,
  0X00FF000000000000, 0XFF00000000000000
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
// Store the length (in bits) of magic numbers for move generation. 
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
// Store all positions bishop and rook pieces can move to on an empty board,
// excluding endpoints.
extern const Bitboard kSliderPieceMasks[kNumSliderMasks][kNumSquares];
// Store masks of the endpoints of rays in slider piece occupancy masks.
extern const Bitboard kSliderEndpointMasks[kNumSliderMasks][kNumSquares];
extern const uint64_t kMagics[kNumSliderMasks][kNumSquares];
extern const unordered_map<uint64_t, Bitboard> kMagicIndexToAttackMap;

#endif // OMEGAZERO_SRC_BOARD_H_
