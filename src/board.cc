/* Noah Himed
*
* Implement the Board type.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#include "board.h"

#include <boost/multiprecision/cpp_int.hpp>

#include <cstdint>
#include <unordered_map>

#include "constants.h"
#include "move.h"

// Debug library
#include <iostream>

using namespace boost::multiprecision;

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
                              int attacking_piece) const {
  Bitboard all_pieces = player_pieces_[kWhite] | player_pieces_[kBlack];
  std::cout << "all_pieces: 0x" << hex << all_pieces << std::endl; 
  Bitboard attack_mask = 0X0;
  int attacked_player = attacking_player ^ 1;
  switch (attacking_piece) {
    case kPawn: {
      Bitboard capture_attacks, push_attacks;
      if (attacking_player == kWhite) {
        capture_attacks = kNonSliderAttackMasks[kWhitePawnTakeAttack][square];
        push_attacks = kNonSliderAttackMasks[kWhitePawnPushAttack][square];
      }
      else if (attacking_player == kBlack) {
        capture_attacks = kNonSliderAttackMasks[kBlackPawnTakeAttack][square];
        push_attacks = kNonSliderAttackMasks[kBlackPawnPushAttack][square];
      }
      // Include capture attacks that attack occupied squares only.
      attack_mask = (capture_attacks & player_pieces_[attacked_player])
                     | push_attacks;
      break;
    }
    case kKnight:
      attack_mask = kNonSliderAttackMasks[kKnightAttack][square];
      break;
    // For bishops and rooks, use the magic bitboard method to get possible moves. The
    // Boost library is used here to avoid integer overflow.
    case kBishop: {
      Bitboard blockers = kSliderPieceMasks[kBishopMoves][square] & all_pieces;
      uint128_t magic = kMagics[kBishopMoves][square];
      uint128_t magic_index_128b = (blockers * magic) >> (kNumSquares - kBishopMagicLengths[square]);
      uint64_t magic_index = static_cast<uint64_t>(magic_index_128b);
      attack_mask = kMagicIndexToAttackMap.at(magic_index);
      break;
    }
    case kRook: {
      Bitboard blockers = kSliderPieceMasks[kRookMoves][square] & all_pieces;
      uint128_t magic = kMagics[kRookMoves][square];
      uint128_t magic_index_128b = (blockers * magic) >> (kNumSquares - kRookMagicLengths[square]);
      uint64_t magic_index = static_cast<uint64_t>(magic_index_128b);
      attack_mask = kMagicIndexToAttackMap.at(magic_index);
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

bool Board::MakeMove(Move move, string& err_msg) {
  // Remove the piece from its starting position in the board.
  Bitboard rm_start_mask = ~(1UL << move.start_sq);
  int start_rank = move.start_sq >> 3;
  int start_file = move.start_sq & 7;
  piece_layout_[start_rank][start_file] = kNA;
  player_layout_[start_rank][start_file] = kNA;
  pieces_[move.moving_piece] &= rm_start_mask;
  player_pieces_[move.moving_player] &= rm_start_mask;

  // Add the piece to its destination square on the board.
  Bitboard end_mask = 1UL << move.end_sq;
  int end_rank = move.end_sq >> 3;
  int end_file = move.end_sq & 7;
  if (move.promoted_piece == kNA) {
    piece_layout_[end_rank][end_file] = move.moving_piece;
    pieces_[move.moving_piece] |= end_mask;
  } else {
    piece_layout_[end_rank][end_file] = move.promoted_piece;
    pieces_[move.promoted_piece] |= end_mask;
  }
  player_layout_[end_rank][end_file] = move.moving_player;
  player_pieces_[move.moving_player] |= end_mask;

  // Remove a captured piece from the board.
  if (move.captured_piece != kNA) {
    pieces_[move.captured_piece] &= ~end_mask;
    int other_player = move.moving_player ^ 1;
    player_pieces_[other_player] &= ~end_mask;
  }

  // Undo the move if it puts the king in check.
  if (KingInCheck()) {
    // TODO: undo the move.
    err_msg = "move puts king in check";
    return false;
  } else {
    return true;
  }
}

int Board::GetPieceOnSquare(int rank, int file) const {
  return piece_layout_[rank][file];
}

int Board::GetPlayerOnSquare(int rank, int file) const {
  return player_layout_[rank][file];
}

Bitboard Board::GetPiecesByType(int piece_type, int player) const {
  if (player == kNA) {
    return pieces_[piece_type];
  } else {
    return pieces_[piece_type] & player_pieces_[player];
  }
}

bool Board::KingInCheck() const {
  // TODO: implement this.
  return false;
}
