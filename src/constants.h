/* Noah Himed
*
* Define constants to be used in other source code files.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#ifndef OMEGAZERO_SRC_CONSTANTS_H_
#define OMEGAZERO_SRC_CONSTANTS_H_

#include <cstdint>

typedef uint64_t Bitboard;

enum BoardSide {
  kQueenSide, kKingSide
};
enum File {
  kFileA, kFileB, kFileC, kFileD, kFileE, kFileF, kFileG, kFileH,
};
enum NonSliderAttackMask {
  kWhitePawnPushAttack, kWhitePawnTakeAttack, kBlackPawnPushAttack,
  kBlackPawnTakeAttack, kKnightAttack, kKingAttack,
};
enum Rank {
  kRank1, kRank2, kRank3, kRank4, kRank5, kRank6, kRank7, kRank8,
};
enum SliderPieceMask {
  kBishopMoves, kRookMoves,
};
// Specify the squares necessary to perform castling moves.
enum Square {
  kSq1 = 0, kSq3 = 2, kSq4 = 3, kSq5 = 4, kSq6 = 5, kSq7 = 6, kSq8 = 7,
  kSq57 = 56, kSq59 = 58, kSq60 = 59, kSq61 = 60, kSq62 = 61, kSq63 = 62,
  kSq64 = 63,
};
enum Piece {
  kPawn, kKnight, kBishop, kRook, kQueen, kKing,
};
enum Player {
  kWhite, kBlack,
};

const int kNA = -1;
const int kNumBitboards = 8;
const int kNumBoardSides = 2;
const int kNumFiles = 8;
const int kNumNonSliderMasks = 6;
const int kNumPieces = 6;
const int kNumPlayers = 2;
const int kNumRanks = 8;
const int kNumSliderMasks = 2;
const int kNumSq = 64;

#endif // OMEGAZERO_SRC_CONSTANTS_H_
