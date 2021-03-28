// Noah Himed
// 18 March 2021
//
// Implement the Board type.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "board.h"
#include "constants.h"
#include "move.h"

#include <cstdint>
#include <unordered_map>

Board::Board() {
  // Index from LSB (square a1) to MSB (square f8).
  pieces_[kWhite]  = 0X000000000000FFFF;
  pieces_[kBlack]  = 0XFFFF000000000000;
  pieces_[kPawn]   = 0X00FF00000000FF00;
  pieces_[kKnight] = 0X2400000000000024;
  pieces_[kBishop] = 0X4200000000000042;
  pieces_[kRook]   = 0X1800000000000018;
  pieces_[kQueen]  = 0X0100000000000001;
  pieces_[kKing]   = 0X8000000000000080;

  for (int rank = k1; rank <= k8; ++rank) {
    for (int file = kA; file <= kH; ++file) {
      layout_[rank][file] = kStartLayout[rank][file];
    }
  }

  int bishop_magic_lengths_[kNumRanks][kNumFiles] = {
    {12, 11, 11, 11, 11, 11, 11, 12},
    {11, 10, 10, 10, 10, 10, 10, 11},
    {11, 10, 10, 10, 10, 10, 10, 11},
    {11, 10, 10, 10, 10, 10, 10, 11},
    {11, 10, 10, 10, 10, 10, 10, 11},
    {11, 10, 10, 10, 10, 10, 10, 11},
    {11, 10, 10, 10, 10, 10, 10, 11},
    {12, 11, 11, 11, 11, 11, 11, 12}
  };

  int rook_magic_lengths_[kNumFiles][kNumRanks] = {
    {6, 5, 5, 5, 5, 5, 5, 6},
    {5, 5, 5, 5, 5, 5, 5, 5},
    {5, 5, 7, 7, 7, 7, 5, 5},
    {5, 5, 7, 9, 9, 7, 5, 5},
    {5, 5, 7, 9, 9, 7, 5, 5},
    {5, 5, 7, 7, 7, 7, 5, 5},
    {5, 5, 5, 5, 5, 5, 5, 5},
    {6, 5, 5, 5, 5, 5, 5, 6}
  };

  std::unordered_map<char, char> hex_map_ = {
    {0b0000, '0'}, {0b0001, '1'}, {0b0010, '2'}, {0b0011, '3'},
    {0b0100, '4'}, {0b0101, '5'}, {0b0110, '6'}, {0b0111, '7'},
    {0b1000, '8'}, {0b1001, '9'}, {0b1010, 'A'}, {0b1011, 'B'},
    {0b1100, 'C'}, {0b1101, 'D'}, {0b1110, 'E'}, {0b1111, 'F'}
  };
}

Bitboard Board::GetAttackMask(Player attacking_player, Piece attacking_piece,
                              Square square) const {
  // TODO: implement this function.
  return 0X0;
}

int Board::GetPieceOnSquare(int rank, int file) const {
  return layout_[rank][file];
}

void UpdateBoard(Move move) {
  // TODO: implement this function.
}

Bitboard Board::GetPiecesByType(Piece piece_type, int player) const {
  if (player == kNA) {
    return pieces_[piece_type];
  } else {
    return pieces_[piece_type] & pieces_[player];
  }
}
