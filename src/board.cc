/* Noah Himed
*
* Implement the Board type.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#include "board.h"

#include <stdint.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <chrono>
#include <random>
#include <unordered_map>

#include "constants.h"
#include "move.h"

// Debug library
#include <iostream>

Board::Board() {
  // Index from LSB (square a1) to MSB (square f8).
  pieces_[kPawn]   = 0X00FF00000000FF00ULL;
  pieces_[kKnight] = 0X4200000000000042ULL;
  pieces_[kBishop] = 0X2400000000000024ULL;
  pieces_[kRook]   = 0X8100000000000081ULL;
  pieces_[kQueen]  = 0X0800000000000008ULL;
  pieces_[kKing]   = 0X1000000000000010ULL;
  player_pieces_[kWhite]  = 0X000000000000FFFFULL;
  player_pieces_[kBlack]  = 0XFFFF000000000000ULL;

  castling_rights_[kWhite][kQueenSide] = true;
  castling_rights_[kWhite][kKingSide] = true;
  castling_rights_[kBlack][kQueenSide] = true;
  castling_rights_[kBlack][kKingSide] = true;
  castling_status_[kWhite][kQueenSide] = false;
  castling_status_[kWhite][kKingSide] = false;
  castling_status_[kBlack][kQueenSide] = false;
  castling_status_[kBlack][kKingSide] = false;

  ep_target_sq_ = kNA;
  halfmove_clock_ = 0;
  for (int rank = kRank8; rank >= kRank1; --rank) {
    for (int file = kFileA; file <= kFileH; ++file) {
      piece_layout_[rank][file] = kInitialPieceLayout[rank][file];
      player_layout_[rank][file] = kInitialPlayerLayout[rank][file];
    }
  }

  // Initialize the Mersenne Twister 64 bit pseudo-random number generator.
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  std::mt19937_64 rand_num_gen(seed);
  // Generate a set of random numbers for Zobrist Hashing.
  for (int player = kWhite; player < kNumPlayers; ++player) {
    for (int board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      castling_rights_rand_nums[player][board_side] = rand_num_gen();
    }
  }
  for (int file = kFileA; file <= kFileH; ++file) {
    ep_file_rand_nums[file] = rand_num_gen();
  }
  for (int piece = kPawn; piece <= kKing; ++piece) {
    for (int rank = kRank1; rank <= kRank8; ++rank) {
      for (int file = kFileA; file <= kFileH; ++file) {
        piece_rand_nums[piece][rank][file] = rand_num_gen();
      }
    }
  }
  black_to_move_rand_num = rand_num_gen();
}

Bitboard Board::GetAttackersToSq(const int& sq,
                                 const int& attacked_player) const {
  Bitboard potential_pawn_attacks;
  int attacking_player = (attacked_player == kWhite) ? kBlack : kWhite;
  // Capture only diagonal squares to sq in the direction of movement.
  if (attacked_player == kWhite) {
    potential_pawn_attacks = kNonSliderAttackMasks[kWhitePawnTakeAttack][sq];
  } else if (attacked_player == kBlack) {
    potential_pawn_attacks = kNonSliderAttackMasks[kBlackPawnTakeAttack][sq];
  }
  // Compute the union (bitwise OR) of all pieces of each type that could
  // capture the square "sq"; each of these bitboards are computed by
  // finding the intersection (bitwise AND) between all spots a piece of a
  // given type could move to from sq, and all the positions that pieces of
  // this type from the opposing player are located.
  return (potential_pawn_attacks & GetPiecesByType(kPawn, attacking_player))
          | (GetAttackMask(attacked_player, sq, kKnight)
             & GetPiecesByType(kKnight, attacking_player))
          | (GetAttackMask(attacked_player, sq, kBishop) 
             & GetPiecesByType(kBishop, attacking_player))
          | (GetAttackMask(attacked_player, sq, kRook) 
             & GetPiecesByType(kRook, attacking_player))
          | (GetAttackMask(attacked_player, sq, kQueen) 
             & GetPiecesByType(kQueen, attacking_player))
          | (GetAttackMask(attacked_player, sq, kKing) 
             & GetPiecesByType(kKing, attacking_player));
}

Bitboard Board::GetAttackMask(const int& attacking_player, const int& sq,
                              const int& attacking_piece) const {
  Bitboard attack_mask = 0X0;
  int attacked_player = (attacking_player == kWhite) ? kBlack : kWhite;
  switch (attacking_piece) {
    case kPawn: {
      Bitboard capture_attacks, push_attacks;
      if (attacking_player == kWhite) {
        capture_attacks = kNonSliderAttackMasks[kWhitePawnTakeAttack][sq];
        push_attacks = kNonSliderAttackMasks[kWhitePawnPushAttack][sq];
      }
      else if (attacking_player == kBlack) {
        capture_attacks = kNonSliderAttackMasks[kBlackPawnTakeAttack][sq];
        push_attacks = kNonSliderAttackMasks[kBlackPawnPushAttack][sq];
      }
      // Include capture attacks that attack occupied squares only.
      attack_mask = (capture_attacks & player_pieces_[attacked_player])
                     | push_attacks;
      break;
    }
    case kKnight:
      attack_mask = kNonSliderAttackMasks[kKnightAttack][sq];
      break;
    // Use the magic bitboard method to get possible moves for bishops and
    // rooks. The Boost library's 128 bit unsigned int data type "uint128_t"
    // is used here to avoid integer overflow.
    case kBishop: {
      Bitboard all_pieces = player_pieces_[kWhite] | player_pieces_[kBlack];
      Bitboard blockers = kSliderPieceMasks[kBishopMoves][sq] & all_pieces;
      if (blockers == 0X0) {
        attack_mask = kUnblockedSliderAttackMasks[kBishopMoves][sq];
      } else {
        boost::multiprecision::uint128_t magic = kMagics[kBishopMoves][sq];
        boost::multiprecision::uint128_t magic_index_128b = 
          (blockers * magic) >> (kNumSq - kBishopMagicLengths[sq]);
        uint64_t magic_index = static_cast<uint64_t>(magic_index_128b);
        attack_mask = kMagicIndexToAttackMap.at(magic_index);
      }
      break;
    }
    case kRook: {
      Bitboard all_pieces = player_pieces_[kWhite] | player_pieces_[kBlack];
      Bitboard blockers = kSliderPieceMasks[kRookMoves][sq] & all_pieces;
      if (blockers == 0X0) {
        attack_mask = kUnblockedSliderAttackMasks[kRookMoves][sq];
      } else {
        boost::multiprecision::uint128_t magic = kMagics[kRookMoves][sq];
        boost::multiprecision::uint128_t magic_index_128b =
          (blockers * magic) >> (kNumSq - kRookMagicLengths[sq]);
        uint64_t magic_index = static_cast<uint64_t>(magic_index_128b);
        attack_mask = kMagicIndexToAttackMap.at(magic_index);
      }
      break;
    }
    // Combine the attack masks of a rook and bishop to get a queen's attack.
    case kQueen: {
      Bitboard bishop_attack = GetAttackMask(attacking_player, sq, kBishop);
      Bitboard rook_attack = GetAttackMask(attacking_player, sq, kRook);
      return bishop_attack | rook_attack;
    }
    case kKing:
      attack_mask = kNonSliderAttackMasks[kKingAttack][sq];
      break;
  }
  return attack_mask;
}

bool Board::GetCastlingRights(const int& player, const int& board_side) const {
  return castling_rights_[player][board_side];
}

bool Board::KingInCheck(const int& player) const {
  // TODO: implement this.
  return false;
}

bool Board::MakeMove(const Move& move, string& err_msg) {
  int start_rank;
  int start_file;
  int end_rank;
  int end_file;
  // Handle all non-castling moves.
  if (move.castling_type == kNA) {
    // Remove a captured piece from the board.
    if (move.captured_piece != kNA) {
      int other_player = (move.moving_player == kWhite) ? kBlack : kWhite;
      // Compute the position to remove a pawn from in an en passent.
      if (move.is_ep) {
        int ep_capture_sq;
        if (move.moving_player == kWhite) {
          ep_capture_sq = kNumFiles * kRank5 + end_file;
          piece_layout_[kRank5][end_file] = kNA;
          player_layout_[kRank5][end_file] = kNA;
        } else if (move.moving_player == kBlack) {
          ep_capture_sq = kNumFiles * kRank4 + end_file;
          piece_layout_[kRank4][end_file] = kNA;
          player_layout_[kRank4][end_file] = kNA;
        }
        Bitboard ep_capture_mask = ~(1ULL << ep_capture_sq);
        pieces_[move.captured_piece] &= ep_capture_mask;
        player_pieces_[other_player] &= ep_capture_mask;
      } else {
        Bitboard piece_capture_mask = ~(1ULL << move.end_sq);
        pieces_[move.captured_piece] &= piece_capture_mask;
        player_pieces_[other_player] &= piece_capture_mask;
      }
    }
    MovePiece(move.moving_player, move.moving_piece, move.start_sq,
              move.end_sq, move.promoted_piece);
  // Handle queenside castling moves.
  } else if (move.castling_type == kQueenSide) {
    if (move.moving_player == kWhite) {
      MovePiece(kWhite, kRook, kSqA1, kSqD1);
      MovePiece(kWhite, kKing, kSqE1, kSqC1);
    } else if (move.moving_player == kBlack) {
      MovePiece(kBlack, kRook, kSqA8, kSqD8);
      MovePiece(kBlack, kKing, kSqE8, kSqC8);
    }
  // Handle kingside castling moves.
  } else if (move.castling_type == kKingSide) {
    if (move.moving_player == kWhite) {
      MovePiece(kWhite, kRook, kSqH1, kSqF1);
      MovePiece(kWhite, kKing, kSqE1, kSqG1);
    } else if (move.moving_player == kBlack) {
      MovePiece(kBlack, kRook, kSqH8, kSqF8);
      MovePiece(kBlack, kKing, kSqE8, kSqG8);
    }
  }

  // Undo the move if it puts the king in check.
  if (KingInCheck(move.moving_player)) {
    // TODO: undo the move.
    err_msg = "move puts king in check";
    return false;
  // Update the en passent target square, castling rights,
  // the halfmove clock, and the repition queue if the move
  // doesn't put the King in check.
  } else {
    ep_target_sq_ = move.new_ep_target_sq;
    // Update castling rights.
    if (move.moving_piece == kKing) {
      castling_rights_[move.moving_player][kQueenSide] = false;
      castling_rights_[move.moving_player][kKingSide] = false;
    } else if (move.moving_piece == kRook) {
      // Check that a rook is moving from its original starting position
      // and that the player still has castling rights on that side before
      // revoking castling rights.
      if ((move.moving_player == kWhite && start_rank == kRank1)
          || (move.moving_player == kBlack && start_rank == kRank8)) {
        if (start_file == kFileA
            && castling_rights_[move.moving_player][kQueenSide]) {
          castling_rights_[move.moving_player][kQueenSide] = false;
        } else if (start_file == kFileH
                   && castling_rights_[move.moving_player][kKingSide]) {
          castling_rights_[move.moving_player][kKingSide] = false;
        }
      }
    }
    // Reset the halfmove clock if a pawn was moved or if a move resulted in a
    // capture.
    if (move.captured_piece != kNA || move.moving_piece == kPawn) {
      halfmove_clock_ = 0;
    } else {
      halfmove_clock_++;
    }
    // TODO: Add to repitition queue
    return true;
  }
}

int Board::GetEpTargetSq() const {
  return ep_target_sq_;
}

int Board::GetHalfmoveClock() const {
  return halfmove_clock_;
}

int Board::GetPieceOnSq(const int& rank, const int& file) const {
  return piece_layout_[rank][file];
}

int Board::GetPlayerOnSq(const int& rank, const int& file) const {
  return player_layout_[rank][file];
}

Bitboard Board::GetPiecesByType(const int& piece_type,
                                const int& player) const {
  if (player == kNA) {
    return pieces_[piece_type];
  } else {
    return pieces_[piece_type] & player_pieces_[player];
  }
}

uint64_t Board::GetBoardHash(const int& side_to_move) const {
  // Use the Zobrist Hashing algorithm to compute a unique hash of the board
  // state. This involves hashing all stored pseudo-random numbers applicable
  // to a given game state.
  uint64_t board_hash = 0;
  for (int player = kWhite; player < kNumPlayers; ++player) {
    for (int board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      if(castling_status_[player][board_side]) {
        board_hash ^= castling_rights_rand_nums[player][board_side];
      }
    }
  }
  if (ep_target_sq_ != kNA) {
    int ep_target_file = GetFileFromSq(ep_target_sq_);
    board_hash ^= ep_file_rand_nums[ep_target_file];
  }
  int piece_type;
  for(int rank = kRank1; rank <= kRank8; ++rank) {
    for (int file = kFileA; file <= kFileH; ++file) {
      piece_type = piece_layout_[rank][file];
      if (piece_type != kNA) {
        board_hash ^= piece_rand_nums[piece_type][rank][file];
      }
    }
  }
  if (side_to_move == kBlack) {
    board_hash ^= black_to_move_rand_num;
  }
  return board_hash;
}

void Board::MovePiece(const int& moving_player, const int& piece,
                      const int& start_sq, const int& end_sq,
                      const int& promoted_piece) {
  // Remove the selected piece from its start position on the board.
  int start_rank = GetRankFromSq(start_sq);
  int start_file = GetFileFromSq(start_sq);
  piece_layout_[start_rank][start_file] = kNA;
  player_layout_[start_rank][start_file] = kNA;
  Bitboard rm_piece_mask = ~(1ULL << start_sq);
  pieces_[piece] &= rm_piece_mask;
  player_pieces_[moving_player] &= rm_piece_mask;
  // Add the selected piece back at its end position on the board.
  Bitboard new_piece_pos_mask = 1ULL << end_sq;
  int end_rank = GetRankFromSq(end_sq);
  int end_file = GetFileFromSq(end_sq);
  if (promoted_piece == kNA) {
    piece_layout_[end_rank][end_file] = piece;
    pieces_[piece] |= new_piece_pos_mask;
  // Add a piece back as the type it promotes to if move is a pawn promotion.
  } else {
    piece_layout_[end_rank][end_file] = promoted_piece;
    pieces_[promoted_piece] |= new_piece_pos_mask;
  }
  player_layout_[end_rank][end_file] = moving_player;
  player_pieces_[moving_player] |= new_piece_pos_mask;
}

int GetFileFromSq(const int& sq) {
  return sq & 7;
}
int GetRankFromSq(const int& sq) {
  return sq >> 3;
}
