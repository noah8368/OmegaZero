// Noah Himed
// 18 March 2021
//
// Define constants to be used in other source code files.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#ifndef OMEGAZERO_SRC_CONSTANTS_H_
#define OMEGAZERO_SRC_CONSTANTS_H_

#include <cstdint>

typedef uint64_t Bitboard;

enum NonSliderAttackMask {
  kWhitePawnPushAttack, kWhitePawnCaptureAttack, kBlackPawnPushAttack,
  kBlackPawnCaptureAttack, kKnightAttack, kKingAttack,
};
enum SliderPieceMask {
  kBishopMoves, kRookMoves,
};
enum Rank {
  k1 = 0, k2 = 1, k3 = 2, k4 = 3, k5 = 4, k6 = 5, k7 = 6, k8 = 7,
};
enum File {
  kA, kB, kC, kD, kE, kF, kG, kH,
};
enum Piece {
  kEmpty, kPawn, kKnight, kBishop, kRook, kQueen, kKing
};
enum Player {
  kWhite = 1, kBlack = -1,
};
// LERF mapped square definitions
enum Square {
  kA1, kB1, kC1, kD1, kE1, kF1, kG1, kH1,
  kA2, kB2, kC2, kD2, kE2, kF2, kG2, kH2,
  kA3, kB3, kC3, kD3, kE3, kF3, kG3, kH3,
  kA4, kB4, kC4, kD4, kE4, kF4, kG4, kH4,
  kA5, kB5, kC5, kD5, kE5, kF5, kG5, kH5,
  kA6, kB6, kC6, kD6, kE6, kF6, kG6, kH6,
  kA7, kB7, kC7, kD7, kE7, kF7, kG7, kH7,
  kA8, kB8, kC8, kD8, kE8, kF8, kG8, kH8
};
enum BoardSide {
  kKingSide, kQueenSide,
};

const int kNA = -1;
const int kNumBitboards = 8;
const int kNumFiles = 8;
const int kNumNonSliderMasks = 6;
const int kNumPieces = 6;
const int kNumRanks = 8;
const int kNumSliderMasks = 2;
const int kNumSquares = 64;

// Little Endian Rank File (LERF) indexed 8x8 board representation
const int kStartLayout[kNumRanks][kNumFiles] = {
  kWhite * kRook, kWhite * kKnight, kWhite * kBishop, kWhite * kQueen,
  kWhite * kKing, kWhite * kBishop, kWhite * kKnight, kWhite * kRook,
  kWhite * kPawn, kWhite * kPawn, kWhite * kPawn, kWhite * kPawn,
  kWhite * kPawn, kWhite * kPawn, kWhite * kPawn, kWhite * kPawn,
  kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty,
  kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty,
  kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty,
  kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty,
  kBlack * kPawn, kBlack * kPawn, kBlack * kPawn, kBlack * kPawn,
  kBlack * kPawn, kBlack * kPawn, kBlack * kPawn, kBlack * kPawn,
  kBlack * kRook, kBlack * kKnight, kBlack * kBishop, kBlack * kQueen,
  kBlack * kKing, kBlack * kBishop, kBlack * kKnight, kBlack * kRook
};

#endif // OMEGAZERO_SRC_CONSTANTS_H_
