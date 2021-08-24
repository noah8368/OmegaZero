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
}

Bitboard Board::GetAttackMask(int attacking_player, int square,
                              int attacking_piece) const {
  Bitboard all_pieces = player_pieces_[kWhite] | player_pieces_[kBlack];
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
    int other_player = move.moving_player ^ 1;
    if (move.is_ep) {
      int ep_capture_sq;
      if (move.moving_player == kWhite) {
        ep_capture_sq = kNumFiles * k5 + end_file;
        piece_layout_[k5][end_file] = kNA;
        player_layout_[k5][end_file] = kNA;
      } else if (move.moving_player == kBlack) {
        ep_capture_sq = kNumFiles * k4 + end_file;
        piece_layout_[k4][end_file] = kNA;
        player_layout_[k4][end_file] = kNA;
      }
      Bitboard ep_capture_mask = ~(1UL << ep_capture_sq);
      pieces_[move.captured_piece] &= ep_capture_mask;
      player_pieces_[other_player] &= ep_capture_mask;
    } else {
      pieces_[move.captured_piece] &= ~end_mask;
      player_pieces_[other_player] &= ~end_mask;
    }
  }

  // Undo the move if it puts the king in check.
  if (KingInCheck()) {
    // TODO: undo the move.
    err_msg = "move puts king in check";
    return false;
  // Update the en passent target square, castling rights,
  // the halfmove clock, and the repition queue if the move
  // doesn't put the King in check.
  } else {
    ep_target_sq_ = move.new_ep_target_sq;
    // TODO: Set castling rights
    // TODO: Set Halfmove clock
    // TODO: Add to repitition queue
    return true;
  }
}

int Board::GetEpTargetSquare() const {
  return ep_target_sq_;
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
