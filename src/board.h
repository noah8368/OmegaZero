/* Noah Himed
*
* Define a Board object, which includes both bitboard and 8x8 board
* representations to store piece locations.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#ifndef OMEGAZERO_SRC_BOARD_H_
#define OMEGAZERO_SRC_BOARD_H_

#include <stdint.h>

#include <unordered_map>

#include "constants.h"
#include "move.h"

using namespace std;

class Board {
public:
  Board();
  Bitboard GetAttackersToSq(const int& sq, const int& attacked_player) const;
  // Return possible attacks a specified piece can make on all other pieces.
  Bitboard GetAttackMask(const int& attacking_player, const int& sq,
                         const int& attacking_piece) const;
  Bitboard GetPiecesByType(const int& piece_type, const int& player) const;
  bool GetCastlingRights(const int& player, const int& board_side) const;
  bool KingInCheck(const int& player) const;
  // Return false if the move was found to put the moving player's King in
  // check. Return true is the board state was succesfully updated.
  bool MakeMove (const Move& move, string& err_msg);
  int GetEpTargetSq() const;
  int GetPieceOnSq(const int& rank, const int& file) const;
  int GetPlayerOnSq(const int& rank, const int& file) const;
private:
  void MovePiece(const int& moving_player, const int& piece,
                 const int& start_sq, const int& end_sq);

  // Store bitboard board representations of each type
  // of piece that are still active in the game.
  Bitboard pieces_[kNumBitboards];
  // Store bitboard board representations of the pieces
  // in each players' posession.
  Bitboard player_pieces_[kNumPlayers];
  bool castling_rights_[kNumPlayers][kNumBoardSides];
  int ep_target_sq_;
  // Store an 8x8 board representation.
  int piece_layout_[kNumRanks][kNumFiles];
  int player_layout_[kNumRanks][kNumFiles];
  int bishop_magic_lengths_[kNumSq];
  int rook_magic_lengths_[kNumSq];
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
const int kBishopMagicLengths[kNumSq] = {
  6, 5, 5, 5, 5, 5, 5, 6,
  5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 7, 7, 7, 7, 5, 5,
  5, 5, 7, 9, 9, 7, 5, 5,
  5, 5, 7, 9, 9, 7, 5, 5,
  5, 5, 7, 7, 7, 7, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5,
  6, 5, 5, 5, 5, 5, 5, 6
};
const int kRookMagicLengths[kNumSq] = {
  12, 11, 11, 11, 11, 11, 11, 12,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  12, 11, 11, 11, 11, 11, 11, 12
};

extern const Bitboard kNonSliderAttackMasks[kNumNonSliderMasks][kNumSq];
// Store all positions bishop and rook pieces can move to on an empty board,
// excluding endpoints.
extern const Bitboard kSliderPieceMasks[kNumSliderMasks][kNumSq];
// Store all positions bishop and rook pieces can move to on an empty board,
// including endpoints.
extern const Bitboard kUnblockedSliderAttackMasks[kNumSliderMasks][kNumSq];
extern const uint64_t kMagics[kNumSliderMasks][kNumSq];
extern const unordered_map<uint64_t, Bitboard> kMagicIndexToAttackMap;

#endif // OMEGAZERO_SRC_BOARD_H_
