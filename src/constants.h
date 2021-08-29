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
  kSqA1 = 0, kSqC1 = 2, kSqD1 = 3, kSqE1 = 4, kSqF1 = 5, kSqG1 = 6, kSqH1 = 7,
  kSqA8 = 56, kSqC8 = 58, kSqD8 = 59, kSqE8 = 60, kSqF8 = 61, kSqG8 = 62,
  kSqH8 = 63,
};
enum Piece {
  kPawn, kKnight, kBishop, kRook, kQueen, kKing,
};
enum Player {
  kWhite, kBlack,
};

const int kHalfmoveClockLimit = 75;
const int kNA = -1;
const int kNumBitboards = 8;
const int kNumBoardSides = 2;
const int kNumFiles = 8;
const int kNumNonSliderMasks = 6;
const int kNumPieceTypes = 6;
const int kNumPlayers = 2;
const int kNumRanks = 8;
const int kNumSliderMasks = 2;
const int kNumSq = 64;

#endif // OMEGAZERO_SRC_CONSTANTS_H_
