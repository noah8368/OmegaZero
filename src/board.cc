// Noah Himed
// 18 March 2021
//
// Implement the Board type.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "board.h"

#include <gmp.h>

#include <cstdint>
#include <unordered_map>

#include "constants.h"
#include "move.h"

// debug libs
#include <iostream>
#include <iomanip>

using namespace std;

Board::Board() {
  // Index from LSB (square a1) to MSB (square f8).
  player_pieces_[kWhite]  = 0X000000000000FFFF;
  player_pieces_[kBlack]  = 0XFFFF000000000000;

  pieces_[kPawn]   = 0X00FF00000000FF00;
  pieces_[kKnight] = 0X4200000000000042;
  pieces_[kBishop] = 0X2400000000000024;
  pieces_[kRook]   = 0X8100000000000081;
  pieces_[kQueen]  = 0X0800000000000008;
  pieces_[kKing]   = 0X1000000000000010;

  for (int rank = k8; rank >= k1; --rank) {
    for (int file = kA; file <= kH; ++file) {
      piece_layout_[rank][file] = kInitialPieceLayout[rank][file];
      player_layout_[rank][file] = kInitialPlayerLayout[rank][file];
    }
  }

  std::unordered_map<char, char> hex_map_ = {
    {0b0000, '0'}, {0b0001, '1'}, {0b0010, '2'}, {0b0011, '3'},
    {0b0100, '4'}, {0b0101, '5'}, {0b0110, '6'}, {0b0111, '7'},
    {0b1000, '8'}, {0b1001, '9'}, {0b1010, 'A'}, {0b1011, 'B'},
    {0b1100, 'C'}, {0b1101, 'D'}, {0b1110, 'E'}, {0b1111, 'F'}
  };
}

Bitboard Board::GetAttackMask(int attacking_player, int square,
                              Piece attacking_piece) const {
  Bitboard all_pieces = pieces_[kWhite] | pieces_[kBlack];
  Bitboard attack_mask = 0X0;
  switch (attacking_piece) {
    case kPawn: {
      Bitboard capture_attacks, push_attacks;
      if (attacking_player == kWhite) {
        capture_attacks = kNonSliderAttackMasks[kWhitePawnCaptureAttack][square];
        push_attacks = kNonSliderAttackMasks[kWhitePawnPushAttack][square];
      }
      else if (attacking_player == kBlack) {
        capture_attacks = kNonSliderAttackMasks[kBlackPawnCaptureAttack][square];
        push_attacks = kNonSliderAttackMasks[kBlackPawnPushAttack][square];
      }
      // Include capture attacks that attack occupied squares only.
      attack_mask = (capture_attacks & all_pieces) | push_attacks;
      break;
    }
    case kKnight:
      attack_mask = kNonSliderAttackMasks[kKnightAttack][square];
      break;
      // For bishops and rooks, avoid overflow by using the mpz library to
      // compute (magic * blocker_mask) >> (kNumSquares - magic_length). Use
      // this as an index for the magic bitboard method.
    case kBishop: {
      Bitboard bishop_mask = kSliderPieceMasks[kBishopMoves][square];
      mpz_t blocker_mask;
      mpz_init_set_ui(blocker_mask, bishop_mask & all_pieces);
      mpz_t magic;
      mpz_init_set_ui(magic, kMagics[kBishopMoves][square]);
      mpz_t mpz_index;
      mpz_init(mpz_index);
      mpz_mul(mpz_index, magic, blocker_mask);
      mp_bitcnt_t rshift_amt = kNumSquares - kBishopMagicLengths[square];
      mpz_fdiv_q_2exp(mpz_index, mpz_index, rshift_amt);
      void* index_ptr = mpz_export(nullptr, nullptr, 1, sizeof(uint64_t), 0, 0,
                                   mpz_index);
      uint64_t index = *static_cast<uint64_t*>(index_ptr);
      mpz_clear(blocker_mask);
      mpz_clear(magic);
      mpz_clear(mpz_index);
      attack_mask = kMagicIndexToAttackMap.at(index);
      break;
    }
    case kRook: {
      Bitboard rook_mask = kSliderPieceMasks[kRookMoves][square];
      mpz_t blocker_mask;
      mpz_init_set_ui(blocker_mask, rook_mask & all_pieces);
      mpz_t magic;
      mpz_init_set_ui(magic, kMagics[kRookMoves][square]);
      mpz_t mpz_index;
      mpz_init(mpz_index);
      mpz_mul(mpz_index, magic, blocker_mask);
      mp_bitcnt_t rshift_amt = kNumSquares - kRookMagicLengths[square];
      mpz_fdiv_q_2exp(mpz_index, mpz_index, rshift_amt);
      void* index_ptr = mpz_export(nullptr, nullptr, 1, sizeof(uint64_t), 0, 0,
                                   mpz_index);
      uint64_t index = *static_cast<uint64_t*>(index_ptr);
      mpz_clear(blocker_mask);
      mpz_clear(magic);
      mpz_clear(mpz_index);
      attack_mask = kMagicIndexToAttackMap.at(index);
      break;
    }
    // Combine the attack masks of a rook and bishop to get a queen's attack.
    case kQueen: {
      Bitboard bishop_attack = GetAttackMask(attacking_player, square, kBishop);
      Bitboard rook_attack = GetAttackMask(attacking_player, square, kRook);
      return bishop_attack | rook_attack;
    }
    case kKing:
      attack_mask = kNonSliderAttackMasks[kKingAttack][square];
      break;
  }
  return attack_mask;
}

int Board::GetPieceOnSquare(int rank, int file) const {
  return piece_layout_[rank][file];
}

int Board::GetPlayerOnSquare(int rank, int file) const {
  return player_layout_[rank][file];
}

void UpdateBoard(Move move) {
  // TODO: implement this function.
}

Bitboard Board::GetPiecesByType(Piece piece_type, int player) const {
  if (player == kNA) {
    return pieces_[piece_type];
  } else {
    return pieces_[piece_type] & player_pieces_[player];
  }
}
