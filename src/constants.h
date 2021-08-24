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

enum NonSliderAttackMask {
  kWhitePawnPushAttack, kWhitePawnTakeAttack, kBlackPawnPushAttack,
  kBlackPawnTakeAttack, kKnightAttack, kKingAttack,
};
enum SliderPieceMask {
  kBishopMoves, kRookMoves,
};
enum Rank {
  k1, k2, k3, k4, k5, k6, k7, k8,
};
enum File {
  kA, kB, kC, kD, kE, kF, kG, kH,
};
enum Player {
  kWhite, kBlack,
};
enum Piece {
  kPawn, kKnight, kBishop, kRook, kQueen, kKing,
};
enum BoardSide {
  kKingSide, kQueenSide,
};

const int kNA = -1;
const int kNumBitboards = 8;
const int kNumFiles = 8;
const int kNumNonSliderMasks = 6;
const int kNumPieces = 6;
const int kNumPlayers = 2;
const int kNumRanks = 8;
const int kNumSliderMasks = 2;
const int kNumSquares = 64;

#endif // OMEGAZERO_SRC_CONSTANTS_H_
