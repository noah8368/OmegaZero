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

enum Rank {
  k1 = 0, k2 = 1, k3 = 2, k4 = 3, k5 = 4, k6 = 5, k7 = 6, k8 = 7,
};
enum File {
  kA, kB, kC, kD, kE, kF, kG, kH,
};
enum Piece {
  kPawn, kKnight, kBishop, kRook, kQueen, kKing, kEmpty
};
enum Player {
  kWhite = 1, kBlack = -1,
};

const int kNumRanks = 8;
const int kNumFiles = 8;
const int kNumBitboards = 8;

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
