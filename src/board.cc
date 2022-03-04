/* Noah Himed
 *
 * Implement the Board type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "board.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "bad_move.h"
#include "move.h"

namespace omegazero {

using std::invalid_argument;
using std::string;

using std::cout;
using std::endl;

typedef boost::multiprecision::uint128_t U128;

Board::Board(const string& init_pos) {
  for (S8 piece_type = kPawn; piece_type <= kKing; ++piece_type) {
    pieces_[piece_type] = 0ULL;
  }
  for (S8 player = kWhite; player <= kBlack; ++player) {
    player_pieces_[player] = 0ULL;
    // Initialize all castling rights to false before parsing the FEN string to
    // set the board, which may reset some castling rights to true.
    for (S8 board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      castling_rights_[player][board_side] = false;
    }
  }
  ep_target_sq_ = kNA;

  // Set the piece positions, castling rights, and player to move.
  InitBoardPos(init_pos);
  InitBoardHash();
}

auto Board::GetAttackMap(S8 attacking_player, S8 sq, S8 attacking_piece) const
    -> Bitboard {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::GetAttackMap()");
  }

  Bitboard attack_map = 0X0;
  S8 attacked_player = GetOtherPlayer(attacking_player);
  switch (attacking_piece) {
    case kPawn: {
      Bitboard capture_attacks;
      Bitboard push_attacks;
      if (attacking_player == kWhite) {
        capture_attacks = kNonSliderAttackMaps[kWhitePawnCapture][sq];
        push_attacks = kNonSliderAttackMaps[kWhitePawnPush][sq];
      } else {
        capture_attacks = kNonSliderAttackMaps[kBlackPawnCapture][sq];
        push_attacks = kNonSliderAttackMaps[kBlackPawnPush][sq];
      }
      // Include captures that attack occupied squares and push moves that move
      // onto empty squares only. Note that the resulting attack map may include
      // squares in front of the pawn occupied by other pieces. This is
      // necessary due to how the function is used by other parts of the engine.
      attack_map =
          (capture_attacks & player_pieces_[attacked_player]) | push_attacks;
      break;
    }
    case kKnight:
      attack_map = kNonSliderAttackMaps[kKnightAttack][sq];
      break;
    // Use the magic bitboard method to get possible moves for bishops and
    // rooks. The Boost library's 128 bit unsigned int data type "U128"
    // is used here to avoid integer overflow.
    case kBishop: {
      Bitboard all_pieces = player_pieces_[kWhite] | player_pieces_[kBlack];
      Bitboard blockers = kSliderPieceMaps[kBishopMoves][sq] & all_pieces;
      if (blockers == 0X0) {
        attack_map = kUnblockedSliderAttackMaps[kBishopMoves][sq];
      } else {
        U128 magic = kMagics[kBishopMoves][sq];
        U128 index = (blockers * magic) >> (kNumSq - kBishopMagicLengths[sq]);
        U64 index_U64 = static_cast<U64>(index);
        attack_map = kMagicIndexToAttackMap.at(index_U64);
      }
      break;
    }
    case kRook: {
      Bitboard all_pieces = player_pieces_[kWhite] | player_pieces_[kBlack];
      Bitboard blockers = kSliderPieceMaps[kRookMoves][sq] & all_pieces;
      if (blockers == 0X0) {
        attack_map = kUnblockedSliderAttackMaps[kRookMoves][sq];
      } else {
        U128 magic = kMagics[kRookMoves][sq];
        U128 index = (blockers * magic) >> (kNumSq - kRookMagicLengths[sq]);
        U64 index_U64 = static_cast<U64>(index);
        attack_map = kMagicIndexToAttackMap.at(index_U64);
      }
      break;
    }
    // Combine the attack maps of a rook and bishop to get a queen's attack.
    case kQueen: {
      Bitboard bishop_attack = GetAttackMap(attacking_player, sq, kBishop);
      Bitboard rook_attack = GetAttackMap(attacking_player, sq, kRook);
      return bishop_attack | rook_attack;
    }
    case kKing:
      attack_map = kNonSliderAttackMaps[kKingAttack][sq];
      break;
    default:
      throw invalid_argument("attacking_piece in Board::GetAttackMap()");
  }

  return attack_map;
}

auto Board::GetPiecesByType(S8 piece_type, S8 player) const -> Bitboard {
  if (piece_type == kNA) {
    if (player == kWhite || player == kBlack) {
      return player_pieces_[player];
    }
    throw invalid_argument("player in Board::GetPiecesByType()");
  }
  if (player == kNA) {
    if (piece_type >= kPawn && piece_type <= kKing) {
      return pieces_[piece_type];
    }
    throw invalid_argument("piece_type in Board::GetPiecesByType()");
  }
  if ((piece_type >= kPawn && piece_type <= kKing) &&
      (player == kWhite || player == kBlack)) {
    return pieces_[piece_type] & player_pieces_[player];
  }

  throw invalid_argument("player, piece_type in Board::GetPiecesByType");
}

auto Board::CastlingLegal(S8 board_side) const -> bool {
  // For castling moves, check that the following hold:
  //   * Neither the king nor the chosen rook has previously moved.
  //   * There are no pieces between the king and the chosen rook.
  //   * The king is not currently in check.
  //   * The king does not pass through a square that is attacked by an enemy
  //     piece.
  if (board_side == kQueenSide) {
    return castling_rights_[player_to_move_][kQueenSide] && !KingInCheck() &&
           ((player_to_move_ == kWhite && piece_layout_[kSqB1] == kNA &&
             piece_layout_[kSqC1] == kNA && piece_layout_[kSqD1] == kNA &&
             GetAttackersToSq(kSqD1, kWhite) == 0X0) ||
            (player_to_move_ == kBlack && GetPieceOnSq(kSqB8) == kNA &&
             piece_layout_[kSqC8] == kNA && piece_layout_[kSqD8] == kNA &&
             GetAttackersToSq(kSqD8, kBlack) == 0X0));
  }
  if (board_side == kKingSide) {
    return castling_rights_[player_to_move_][kKingSide] && !KingInCheck() &&
           ((player_to_move_ == kWhite && piece_layout_[kSqF1] == kNA &&
             GetAttackersToSq(kSqF1, kWhite) == 0X0 &&
             piece_layout_[kSqG1] == kNA) ||
            (player_to_move_ == kBlack && GetPieceOnSq(kSqF8) == kNA &&
             GetAttackersToSq(kSqF8, kBlack) == 0X0 &&
             piece_layout_[kSqG8] == kNA));
  }

  throw invalid_argument("board_side in Board::CastlingLegal()");
}

auto Board::DoublePawnPushLegal(S8 file) const -> bool {
  if (!FileOnBoard(file)) {
    throw invalid_argument("file in Board::DoublePawnPushLegal()");
  }

  if (player_to_move_ == kWhite) {
    S8 rank3_double_pawn_push_sq = GetSqFromRankFile(kRank3, file);
    S8 rank2_double_pawn_push_sq = GetSqFromRankFile(kRank2, file);
    return piece_layout_[rank3_double_pawn_push_sq] == kNA &&
           piece_layout_[rank2_double_pawn_push_sq] == kPawn &&
           player_layout_[rank2_double_pawn_push_sq] == kWhite;
  }
  // Handle the case of evaluating if a double pawn push from black is legal.
  S8 rank6_double_pawn_push_sq = GetSqFromRankFile(kRank6, file);
  S8 rank7_double_pawn_push_sq = GetSqFromRankFile(kRank7, file);
  return piece_layout_[rank6_double_pawn_push_sq] == kNA &&
         piece_layout_[rank7_double_pawn_push_sq] == kPawn &&
         player_layout_[rank7_double_pawn_push_sq] == kBlack;
}

auto Board::Evaluate() const -> int {
  Bitboard white_pieces;
  Bitboard black_pieces;
  int board_score = 0;
  for (S8 piece_type = kPawn; piece_type <= kKing; ++piece_type) {
    white_pieces = GetPiecesByType(piece_type, kWhite);
    black_pieces = GetPiecesByType(piece_type, kBlack);
    board_score += kPieceVals[piece_type] *
                   (GetNumSetSq(white_pieces) - GetNumSetSq(black_pieces));
  }
  S8 moving_side = (player_to_move_ == kWhite) ? 1 : -1;
  return board_score * moving_side;
}

auto Board::MakeMove(const Move& move) -> void {
  if (move.castling_type == kNA) {
    MakeNonCastlingMove(move);
  } else if (move.castling_type == kQueenSide) {
    // Make queenside castling moves.
    if (player_to_move_ == kWhite) {
      MovePiece(kRook, kSqA1, kSqD1);
      MovePiece(kKing, kSqE1, kSqC1);
    } else if (player_to_move_ == kBlack) {
      MovePiece(kRook, kSqA8, kSqD8);
      MovePiece(kKing, kSqE8, kSqC8);
    }
  } else if (move.castling_type == kKingSide) {
    // Make kingside castling moves.
    if (player_to_move_ == kWhite) {
      MovePiece(kRook, kSqH1, kSqF1);
      MovePiece(kKing, kSqE1, kSqG1);
    } else if (player_to_move_ == kBlack) {
      MovePiece(kRook, kSqH8, kSqF8);
      MovePiece(kKing, kSqE8, kSqG8);
    }
  }

  // Update the en passent target square and the board hash to reflect a change
  // in the file of the en passent target square.
  ep_target_sq_history_.push(ep_target_sq_);
  S8 prev_ep_target_file =
      (ep_target_sq_ == kNA) ? kNA : GetFileFromSq(ep_target_sq_);
  ep_target_sq_ = move.new_ep_target_sq;
  S8 curr_ep_target_file =
      (ep_target_sq_ == kNA) ? kNA : GetFileFromSq(ep_target_sq_);
  if (prev_ep_target_file != curr_ep_target_file) {
    if (prev_ep_target_file != kNA) {
      board_hash_ ^= ep_file_rand_nums_[prev_ep_target_file];
    }
    if (curr_ep_target_file != kNA) {
      board_hash_ ^= ep_file_rand_nums_[curr_ep_target_file];
    }
  }

  // Reset the halfmove clock if a pawn was moved or if a move resulted in a
  // capture.
  halfmove_clock_history_.push(halfmove_clock_);
  if (move.captured_piece != kNA || move.moving_piece == kPawn) {
    halfmove_clock_ = 0;
  } else {
    ++halfmove_clock_;
  }

  UpdateCastlingRights(move);

  // Undo the move if it puts the king in check.
  if (KingInCheck()) {
    SwitchPlayer();
    UnmakeMove(move);
    throw BadMove("move leaves king in check");
  }

  SwitchPlayer();
  // Update the board hash to reflect player turnover.
  board_hash_ ^= black_to_move_rand_num_;
}

// Assume the passed move has been made use MakeMove(). Calling UnmakeMove()
// on a move that wasn't already made will result in undefined behavior.
auto Board::UnmakeMove(const Move& move) -> void {
  // Revert back to the previous player.
  SwitchPlayer();
  // Update the board hash to reflect player turnover.
  board_hash_ ^= black_to_move_rand_num_;

  if (move.castling_type == kNA) {
    // Undo all non-castling moves.
    UnmakeNonCastlingMove(move);
  } else if (move.castling_type == kQueenSide) {
    // Undo queenside castling moves.
    if (player_to_move_ == kWhite) {
      MovePiece(kRook, kSqD1, kSqA1);
      MovePiece(kKing, kSqC1, kSqE1);
    } else if (player_to_move_ == kBlack) {
      MovePiece(kRook, kSqD8, kSqA8);
      MovePiece(kKing, kSqC8, kSqE8);
    }
  } else if (move.castling_type == kKingSide) {
    // Undo kingside castling moves.
    if (player_to_move_ == kWhite) {
      MovePiece(kRook, kSqF1, kSqH1);
      MovePiece(kKing, kSqG1, kSqE1);
    } else if (player_to_move_ == kBlack) {
      MovePiece(kRook, kSqF8, kSqH8);
      MovePiece(kKing, kSqG8, kSqE8);
    }
  }

  // Revert the halfmove clock.
  halfmove_clock_ = halfmove_clock_history_.top();
  halfmove_clock_history_.pop();

  // Revert the en passent target square and update the board hash.
  if (ep_target_sq_ != kNA) {
    S8 ep_file = GetFileFromSq(ep_target_sq_);
    board_hash_ ^= ep_file_rand_nums_[ep_file];
  }
  ep_target_sq_ = ep_target_sq_history_.top();
  ep_target_sq_history_.pop();
  if (ep_target_sq_ != kNA) {
    S8 ep_file = GetFileFromSq(ep_target_sq_);
    board_hash_ ^= ep_file_rand_nums_[ep_file];
  }

  // Revert all castling rights and update the board hash.
  if (castling_rights_[kWhite][kQueenSide] !=
      white_queenside_castling_rights_history_.top()) {
    board_hash_ ^= castling_rights_rand_nums_[kWhite][kQueenSide];
    castling_rights_[kWhite][kQueenSide] =
        white_queenside_castling_rights_history_.top();
  }
  white_queenside_castling_rights_history_.pop();
  if (castling_rights_[kWhite][kKingSide] !=
      white_kingside_castling_rights_history_.top()) {
    board_hash_ ^= castling_rights_rand_nums_[kWhite][kKingSide];
    castling_rights_[kWhite][kKingSide] =
        white_kingside_castling_rights_history_.top();
  }
  white_kingside_castling_rights_history_.pop();
  if (castling_rights_[kBlack][kQueenSide] !=
      black_queenside_castling_rights_history_.top()) {
    board_hash_ ^= castling_rights_rand_nums_[kBlack][kQueenSide];
    castling_rights_[kBlack][kQueenSide] =
        black_queenside_castling_rights_history_.top();
  }
  black_queenside_castling_rights_history_.pop();
  if (castling_rights_[kBlack][kKingSide] !=
      black_kingside_castling_rights_history_.top()) {
    board_hash_ ^= castling_rights_rand_nums_[kBlack][kKingSide];
    castling_rights_[kBlack][kKingSide] =
        black_kingside_castling_rights_history_.top();
  }
  black_kingside_castling_rights_history_.pop();
}

// Implemement private member functions.

auto Board::GetAttackersToSq(S8 sq, S8 attacked_player) const -> Bitboard {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::GetAttackersToSq()");
  }

  Bitboard potential_pawn_attacks;
  S8 attacking_player = GetOtherPlayer(attacked_player);
  // Capture only diagonal squares to sq in the direction of movement.
  if (attacked_player == kWhite) {
    potential_pawn_attacks = kNonSliderAttackMaps[kWhitePawnCapture][sq];
  } else {
    potential_pawn_attacks = kNonSliderAttackMaps[kBlackPawnCapture][sq];
  }
  // Compute the union (bitwise OR) of all pieces of each type that could
  // capture the square "sq"; each of these bitboards are computed by
  // finding the intersection (bitwise AND) between all spots a piece of a
  // given type could move to from sq, and all the positions that pieces of
  // this type from the opposing player are located.
  return (potential_pawn_attacks & GetPiecesByType(kPawn, attacking_player)) |
         (GetAttackMap(attacked_player, sq, kKnight) &
          GetPiecesByType(kKnight, attacking_player)) |
         (GetAttackMap(attacked_player, sq, kBishop) &
          GetPiecesByType(kBishop, attacking_player)) |
         (GetAttackMap(attacked_player, sq, kRook) &
          GetPiecesByType(kRook, attacking_player)) |
         (GetAttackMap(attacked_player, sq, kQueen) &
          GetPiecesByType(kQueen, attacking_player)) |
         (GetAttackMap(attacked_player, sq, kKing) &
          GetPiecesByType(kKing, attacking_player));
}

auto Board::AddPiece(S8 piece_type, S8 player, S8 sq) -> void {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::AddPiece()");
  }

  if (piece_type == kNA && player == kNA) {
    piece_layout_[sq] = kNA;
    player_layout_[sq] = kNA;
  } else if (piece_type >= kPawn && piece_type <= kKing &&
             (player == kWhite || player == kBlack)) {
    Bitboard piece_mask = 1ULL << sq;
    pieces_[piece_type] |= piece_mask;
    player_pieces_[player] |= piece_mask;
    piece_layout_[sq] = piece_type;
    player_layout_[sq] = player;
  } else {
    throw invalid_argument("piece_type, player in Board::AddPiece()");
  }
}

auto Board::InitBoardHash() -> void {
  board_hash_ = 0X0;
  // Initialize the Mersenne Twister 64 bit pseudo-random number generator.
  U64 seed = std::chrono::system_clock::now().time_since_epoch().count();
  std::mt19937_64 rand_num_gen(seed);
  // Generate a set of random numbers for Zobrist Hashing.
  for (S8 player = kWhite; player < kNumPlayers; ++player) {
    for (S8 board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      castling_rights_rand_nums_[player][board_side] = rand_num_gen();
      if (castling_rights_[player][board_side]) {
        board_hash_ ^= castling_rights_rand_nums_[player][board_side];
      }
    }
  }
  for (S8 file = kFileA; file <= kFileH; ++file) {
    ep_file_rand_nums_[file] = rand_num_gen();
  }
  // Update the hash using the current en passent target square.
  if (ep_target_sq_ != kNA) {
    S8 ep_target_file = GetFileFromSq(ep_target_sq_);
    board_hash_ ^= ep_file_rand_nums_[ep_target_file];
  }
  S8 piece_type;
  for (S8 piece = kPawn; piece <= kKing; ++piece) {
    for (S8 sq = kSqA1; sq <= kSqH8; ++sq) {
      piece_rand_nums_[piece][sq] = rand_num_gen();
      // Update the hash using the current piece placement.
      piece_type = piece_layout_[sq];
      if (piece_type != kNA) {
        board_hash_ ^= piece_rand_nums_[piece_type][sq];
      }
    }
  }
  black_to_move_rand_num_ = rand_num_gen();
  // Update the hash using the side to move.
  if (player_to_move_ == kBlack) {
    board_hash_ ^= black_to_move_rand_num_;
  }
}

auto Board::InitBoardPos(const std::string& init_pos) -> void {
  S8 FEN_field = 0;
  S8 current_sq = kSqA8;
  for (char ch : init_pos) {
    // Keep track of which of the six fields is currently being parsed.
    if (ch == ' ') {
      ++FEN_field;
      continue;
    }

    if (FEN_field == 0) {
      if (ch != '/' && !SqOnBoard(current_sq)) {
        throw invalid_argument("board initialization FEN string");
      }

      // Add pieces to the board.
      if (isalpha(ch)) {
        switch (ch) {
          // Check for white pieces.
          case 'P':
            AddPiece(kPawn, kWhite, current_sq);
            break;
          case 'N':
            AddPiece(kKnight, kWhite, current_sq);
            break;
          case 'B':
            AddPiece(kBishop, kWhite, current_sq);
            break;
          case 'R':
            AddPiece(kRook, kWhite, current_sq);
            break;
          case 'Q':
            AddPiece(kQueen, kWhite, current_sq);
            break;
          case 'K':
            AddPiece(kKing, kWhite, current_sq);
            break;
          // Check for black pieces.
          case 'p':
            AddPiece(kPawn, kBlack, current_sq);
            break;
          case 'n':
            AddPiece(kKnight, kBlack, current_sq);
            break;
          case 'b':
            AddPiece(kBishop, kBlack, current_sq);
            break;
          case 'r':
            AddPiece(kRook, kBlack, current_sq);
            break;
          case 'q':
            AddPiece(kQueen, kBlack, current_sq);
            break;
          case 'k':
            AddPiece(kKing, kBlack, current_sq);
            break;
          default:
            throw invalid_argument("board initialization FEN string");
        }
        ++current_sq;
      } else if (ch - '0' >= 0 && ch - '0' <= 8) {
        S8 empty_sq_count = static_cast<S8>(ch - '0');
        S8 empty_sq = current_sq;
        for (; empty_sq < current_sq + empty_sq_count; ++empty_sq) {
          if (!SqOnBoard(empty_sq)) {
            throw invalid_argument("board initialization FEN string");
          }
          AddPiece(kNA, kNA, empty_sq);
        }
        current_sq = empty_sq;
      } else if (ch == '/') {
        // Set the current square to the rank below the current position.
        current_sq = static_cast<S8>(current_sq - 2 * kNumFiles);
      } else {
        throw invalid_argument("board initialization FEN string");
      }
    } else if (FEN_field == 1) {
      // Record the player to move.
      if (ch == 'w') {
        player_to_move_ = kWhite;
      } else if (ch == 'b') {
        player_to_move_ = kBlack;
      } else {
        throw invalid_argument("board initialization FEN string");
      }
    } else if (FEN_field == 2) {
      // Assign castling rights for each player and board side.
      switch (ch) {
        case 'Q':
          castling_rights_[kWhite][kQueenSide] = true;
          break;
        case 'K':
          castling_rights_[kWhite][kKingSide] = true;
          break;
        case 'q':
          castling_rights_[kBlack][kQueenSide] = true;
          break;
        case 'k':
          castling_rights_[kBlack][kKingSide] = true;
          break;
        case '-':
          break;
        default:
          throw invalid_argument("board initialization FEN string");
      }
    } else if (FEN_field == 3) {
      // Assign the en passent target square.
      if (ep_target_sq_ == kNA) {
        S8 ep_target_sq_file = static_cast<S8>(ch - 'a');
        if (FileOnBoard(ep_target_sq_file)) {
          ep_target_sq_ = ep_target_sq_file;
        } else if (ch != '-') {
          throw invalid_argument("board initialization FEN string");
        }
      } else {
        // Assume ep_target_sq_ is initialized to the target
        // square's file.
        S8 ep_target_sq_rank = static_cast<S8>(ch - '1');
        if (RankOnBoard(ep_target_sq_rank)) {
          ep_target_sq_ = GetSqFromRankFile(ep_target_sq_rank, ep_target_sq_);
        } else {
          throw invalid_argument("board initialization FEN string");
        }
      }
    } else if (FEN_field == 4) {
      // Initialize the halfmove clock.
      if (isdigit(ch)) {
        S8 next_digit = static_cast<S8>(ch - '0');
        if (halfmove_clock_ == kNA) {
          halfmove_clock_ = next_digit;
        } else {
          halfmove_clock_ = static_cast<S8>(10 * halfmove_clock_ + next_digit);
        }
      } else {
        throw invalid_argument("board initialization FEN string");
      }
    } else if (FEN_field == 5) {
      // Ignore the fullmove counter.
      ;
    } else {
      throw invalid_argument("board initialization FEN string");
    }
  }
}

auto Board::MakeNonCastlingMove(const Move& move) -> void {
  // Remove a captured piece from the board.
  if (move.captured_piece != kNA) {
    S8 other_player = GetOtherPlayer(player_to_move_);
    if (move.is_ep) {
      S8 ep_capture_sq = 0X0;
      S8 target_file = GetFileFromSq(move.target_sq);
      // Compute the position for a captured pawn in an en passent,
      // and then remove it from the board.
      if (player_to_move_ == kWhite) {
        ep_capture_sq = GetSqFromRankFile(kRank5, target_file);
        piece_layout_[ep_capture_sq] = kNA;
        player_layout_[ep_capture_sq] = kNA;
      } else if (player_to_move_ == kBlack) {
        ep_capture_sq = GetSqFromRankFile(kRank4, target_file);
        piece_layout_[ep_capture_sq] = kNA;
        player_layout_[ep_capture_sq] = kNA;
      }
      Bitboard ep_capture_mask = ~(1ULL << ep_capture_sq);
      pieces_[kPawn] &= ep_capture_mask;
      player_pieces_[other_player] &= ep_capture_mask;
      // Update the board hash to reflect piece removal.
      board_hash_ ^= piece_rand_nums_[kPawn][ep_capture_sq];
    } else {
      // Remove the captured piece from the board.
      Bitboard piece_capture_mask = ~(1ULL << move.target_sq);
      pieces_[move.captured_piece] &= piece_capture_mask;
      player_pieces_[other_player] &= piece_capture_mask;
      // Update the board hash to reflect piece removal.
      board_hash_ ^= piece_rand_nums_[move.captured_piece][move.target_sq];
    }
  }

  MovePiece(move.moving_piece, move.start_sq, move.target_sq,
            move.promoted_to_piece);
}

auto Board::MovePiece(S8 piece, S8 start_sq, S8 target_sq, S8 promoted_to_piece)
    -> void {
  if (piece < kPawn || piece > kKing) {
    throw invalid_argument("piece in Board::MovePiece()");
  } else if (!SqOnBoard(start_sq)) {
    throw invalid_argument("start_sq in Board::MovePiece()");
  } else if (!SqOnBoard(target_sq)) {
    throw invalid_argument("target_sq in Board::MovePiece()");
  } else if (promoted_to_piece != kNA &&
             (promoted_to_piece <= kPawn || promoted_to_piece >= kKing)) {
    throw invalid_argument("promoted_to_piece in Board::MovePiece()");
  }

  // Remove the selected piece from its start position on the board.
  piece_layout_[start_sq] = kNA;
  player_layout_[start_sq] = kNA;
  Bitboard rm_piece_mask = ~(1ULL << start_sq);
  pieces_[piece] &= rm_piece_mask;
  player_pieces_[player_to_move_] &= rm_piece_mask;
  // Update the board hash to reflect piece removal.
  board_hash_ ^= piece_rand_nums_[piece][start_sq];

  // Add the selected piece back at its target position on the board and update
  // the board hash to reflect piece addition.
  Bitboard new_piece_pos_mask = 1ULL << target_sq;
  if (promoted_to_piece == kNA) {
    pieces_[piece] |= new_piece_pos_mask;
    piece_layout_[target_sq] = piece;
    board_hash_ ^= piece_rand_nums_[piece][target_sq];
  } else {
    // Add a piece back as the type it promotes to if move is a pawn promotion.
    pieces_[promoted_to_piece] |= new_piece_pos_mask;
    piece_layout_[target_sq] = promoted_to_piece;
    board_hash_ ^= piece_rand_nums_[promoted_to_piece][target_sq];
  }

  player_layout_[target_sq] = player_to_move_;
  player_pieces_[player_to_move_] |= new_piece_pos_mask;
}

auto Board::UnmakeNonCastlingMove(const Move& move) -> void {
  // Move the moving piece back to its original position and undo
  // any pawn promotion.
  if (move.promoted_to_piece == kNA) {
    MovePiece(move.moving_piece, move.target_sq, move.start_sq);
  } else {
    // Remove the promoted-to piece from the board.
    Bitboard piece_promotion_rm_mask = ~(1ULL << move.target_sq);
    pieces_[move.promoted_to_piece] &= piece_promotion_rm_mask;
    player_pieces_[player_to_move_] &= piece_promotion_rm_mask;
    piece_layout_[move.target_sq] = kNA;
    player_layout_[move.target_sq] = kNA;
    // Update the board hash to reflect piece removal.
    board_hash_ ^= piece_rand_nums_[move.promoted_to_piece][move.target_sq];

    // Add the original pawn back to its start position.
    Bitboard og_piece_pos_mask = 1ULL << move.start_sq;
    pieces_[kPawn] |= og_piece_pos_mask;
    player_pieces_[player_to_move_] |= og_piece_pos_mask;
    piece_layout_[move.start_sq] = kPawn;
    player_layout_[move.start_sq] = player_to_move_;
    // Update the board hash to reflect piece addition.
    board_hash_ ^= piece_rand_nums_[kPawn][move.start_sq];
  }

  // Place a captured piece back onto the board.
  if (move.captured_piece != kNA) {
    S8 other_player = GetOtherPlayer(player_to_move_);
    if (move.is_ep) {
      S8 ep_capture_sq = 0X0;
      S8 target_file = GetFileFromSq(move.target_sq);
      // Place a captured pawn back onto the board after an en passent.
      if (player_to_move_ == kWhite) {
        ep_capture_sq = GetSqFromRankFile(kRank5, target_file);
        piece_layout_[ep_capture_sq] = kPawn;
        player_layout_[ep_capture_sq] = kBlack;
      } else if (player_to_move_ == kBlack) {
        ep_capture_sq = GetSqFromRankFile(kRank4, target_file);
        piece_layout_[ep_capture_sq] = kPawn;
        player_layout_[ep_capture_sq] = kWhite;
      }
      Bitboard undo_ep_capture_mask = 1ULL << ep_capture_sq;
      pieces_[kPawn] |= undo_ep_capture_mask;
      player_pieces_[other_player] |= undo_ep_capture_mask;
      // Update the board hash to reflect piece addition.
      board_hash_ ^= piece_rand_nums_[kPawn][ep_capture_sq];
    } else {
      Bitboard undo_capture_mask = 1ULL << move.target_sq;
      // Add the captured piece back to its original position.
      pieces_[move.captured_piece] |= undo_capture_mask;
      player_pieces_[other_player] |= undo_capture_mask;
      piece_layout_[move.target_sq] = move.captured_piece;
      player_layout_[move.target_sq] = other_player;
      // Update the board hash to reflect piece addition.
      board_hash_ ^= piece_rand_nums_[move.captured_piece][move.target_sq];
    }
  }
}

auto Board::UpdateCastlingRights(const Move& move) -> void {
  // Record the current castling rights before updating them.
  white_queenside_castling_rights_history_.push(
      castling_rights_[kWhite][kQueenSide]);
  white_kingside_castling_rights_history_.push(
      castling_rights_[kWhite][kKingSide]);
  black_queenside_castling_rights_history_.push(
      castling_rights_[kBlack][kQueenSide]);
  black_kingside_castling_rights_history_.push(
      castling_rights_[kBlack][kKingSide]);

  if (move.castling_type != kNA || move.moving_piece == kKing) {
    // Revoke all castling rights for a player after moving the king.
    if (castling_rights_[player_to_move_][kQueenSide]) {
      castling_rights_[player_to_move_][kQueenSide] = false;
      board_hash_ ^= castling_rights_rand_nums_[player_to_move_][kQueenSide];
    }
    if (castling_rights_[player_to_move_][kKingSide]) {
      castling_rights_[player_to_move_][kKingSide] = false;
      board_hash_ ^= castling_rights_rand_nums_[player_to_move_][kKingSide];
    }
  } else if (move.moving_piece == kRook) {
    S8 start_rank = GetRankFromSq(move.start_sq);
    S8 start_file = GetFileFromSq(move.start_sq);
    // Check that a rook is moving from its original starting position
    // and that the player still has castling rights on that side before
    // revoking castling rights.
    if ((player_to_move_ == kWhite && start_rank == kRank1) ||
        (player_to_move_ == kBlack && start_rank == kRank8)) {
      if (start_file == kFileA &&
          castling_rights_[player_to_move_][kQueenSide]) {
        castling_rights_[player_to_move_][kQueenSide] = false;
        board_hash_ ^= castling_rights_rand_nums_[player_to_move_][kQueenSide];
      } else if (start_file == kFileH &&
                 castling_rights_[player_to_move_][kKingSide]) {
        castling_rights_[player_to_move_][kKingSide] = false;
        board_hash_ ^= castling_rights_rand_nums_[player_to_move_][kKingSide];
      }
    }
  }

  if (move.captured_piece == kRook) {
    // Revoke the other player's castling rights if a player's rook is captured.
    if (player_to_move_ == kWhite) {
      if (move.target_sq == kSqA8 && castling_rights_[kBlack][kQueenSide]) {
        castling_rights_[kBlack][kQueenSide] = false;
        board_hash_ ^= castling_rights_rand_nums_[kBlack][kQueenSide];
      } else if (move.target_sq == kSqH8 &&
                 castling_rights_[kBlack][kKingSide]) {
        castling_rights_[kBlack][kKingSide] = false;
        board_hash_ ^= castling_rights_rand_nums_[kBlack][kKingSide];
      }
    } else if (player_to_move_ == kBlack) {
      if (move.target_sq == kSqA1 && castling_rights_[kWhite][kQueenSide]) {
        castling_rights_[kWhite][kQueenSide] = false;
        board_hash_ ^= castling_rights_rand_nums_[kWhite][kQueenSide];
      } else if (move.target_sq == kSqH1 &&
                 castling_rights_[kWhite][kKingSide]) {
        castling_rights_[kWhite][kKingSide] = false;
        board_hash_ ^= castling_rights_rand_nums_[kWhite][kKingSide];
      }
    }
  }
}

}  // namespace omegazero
