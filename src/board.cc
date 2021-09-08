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
#include <stdexcept>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>

#include "move.h"

using namespace std;

typedef boost::multiprecision::uint128_t U128;

Board::Board(int& player_to_move, const string& init_pos) {
  for (int piece_type = kPawn; piece_type <= kKing; ++piece_type) {
    pieces_[piece_type] = 0ULL;
  }
  for (int player = kWhite; player <= kBlack; ++player) {
    player_pieces_[player] = 0ULL;
    for (int board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      castling_rights_[player][board_side] = false;
      castling_status_[player][board_side] = false;
    }
  }
  ep_target_sq_ = kNA;
  halfmove_clock_ = kNA;

  // Parse a FEN string to initialize the board state.
  int FEN_field = 0;
  int current_sq = kSqA8;
  for (char ch : init_pos) {
    // Keep track of which of the six fields is currently being parsed.
    if (ch == ' ') {
      FEN_field++;
      continue;
    }

    if (FEN_field == 0) {
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
            throw invalid_argument("init_pos in Board::Board() 1");
        }
        current_sq++;
      } else if (ch - '0' >= 0 && ch - '0' <= 8) {
        int empty_sq_count = ch - '0';
        int empty_sq = current_sq;
        for (;empty_sq < current_sq + empty_sq_count; ++empty_sq) {
          AddPiece(kNA, kNA, empty_sq);
        }
        current_sq = empty_sq;
      } else if (ch == '/') {
        // Set the current square to the rank below the current position.
        current_sq -= 2*kNumFiles;
      } else {
        throw invalid_argument("init_pos in Board::Board() 2");
      }
    } else if (FEN_field == 1) {
      // Record the player to move.
      if (ch == 'w') {
        player_to_move = kWhite;
      } else if (ch == 'b') {
        player_to_move = kBlack;
      } else {
        throw invalid_argument("init_pos in Board::Board() 3");
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
          throw invalid_argument("init_pos in Board::Board() 4");
      }
    } else if (FEN_field == 3) {
      // Assign the en passent target square.
      if (ep_target_sq_ == kNA) {
        int ep_target_sq_file = ch - 'a';
        if (FileOnBoard(ep_target_sq_file)) {
          ep_target_sq_ = ep_target_sq_file;
        } else if (ch != '-') {
          throw invalid_argument("init_pos in Board::Board() 5");
        }
      } else {
        // Assume ep_target_sq_ is initialized to the target
        // square's file.
        int ep_target_sq_rank = ch - '1';
        if (RankOnBoard(ep_target_sq_rank)) {
          ep_target_sq_ = GetSqFromRankFile(ep_target_sq_rank, ep_target_sq_);
        } else {
          throw invalid_argument("init_pos in Board::Board() 6");
        }
      }
    } else if (FEN_field == 4) {
      // Initialize the halfmove clock.
      if (isdigit(ch)) {
        int next_digit = ch - '0';
        if (halfmove_clock_ == kNA) {
          halfmove_clock_ = next_digit;
        } else {
          halfmove_clock_ = 10 * halfmove_clock_ + next_digit;
        }
      } else {
        throw invalid_argument("init_pos in Board::Board() 7");
      }
    } else if (FEN_field == 5) {
      // Ignore the fullmove counter.
      ;
    } else {
      throw invalid_argument("init_pos in Board::Board() 8");
    }
  }

  // Initialize the Mersenne Twister 64 bit pseudo-random number generator.
  long int seed = chrono::system_clock::now().time_since_epoch().count();
  mt19937_64 rand_num_gen(seed);
  // Generate a set of random numbers for Zobrist Hashing.
  for (int player = kWhite; player < kNumPlayers; ++player) {
    for (int board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      castling_rights_rand_nums_[player][board_side] = rand_num_gen();
    }
  }
  for (int file = kFileA; file <= kFileH; ++file) {
    ep_file_rand_nums_[file] = rand_num_gen();
  }
  for (int piece = kPawn; piece <= kKing; ++piece) {
    for (int sq = kSqA1; sq <= kSqH8; ++sq) {
      piece_rand_nums_[piece][sq] = rand_num_gen();
    }
  }
  black_to_move_rand_num_ = rand_num_gen();
}

Board::Board(const Board& src) {
  CopyBoard(src);
}

Board& Board::operator=(const Board& src) {
  CopyBoard(src);
  return *this;
}

Bitboard Board::GetAttackersToSq(const int& sq,
                                 const int& attacked_player) const {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::GetAttackersToSq()");
  }

  Bitboard potential_pawn_attacks;
  int attacking_player = GetOtherPlayer(attacked_player);
  // Capture only diagonal squares to sq in the direction of movement.
  if (attacked_player == kWhite) {
    potential_pawn_attacks = kNonSliderAttackMaps[kWhitePawnCapture][sq];
  } else if (attacked_player == kBlack) {
    potential_pawn_attacks = kNonSliderAttackMaps[kBlackPawnCapture][sq];
  }
  // Compute the union (bitwise OR) of all pieces of each type that could
  // capture the square "sq"; each of these bitboards are computed by
  // finding the intersection (bitwise AND) between all spots a piece of a
  // given type could move to from sq, and all the positions that pieces of
  // this type from the opposing player are located.
  return (potential_pawn_attacks & GetPiecesByType(kPawn, attacking_player))
          | (GetAttackMap(attacked_player, sq, kKnight)
             & GetPiecesByType(kKnight, attacking_player))
          | (GetAttackMap(attacked_player, sq, kBishop)
             & GetPiecesByType(kBishop, attacking_player))
          | (GetAttackMap(attacked_player, sq, kRook)
             & GetPiecesByType(kRook, attacking_player))
          | (GetAttackMap(attacked_player, sq, kQueen)
             & GetPiecesByType(kQueen, attacking_player))
          | (GetAttackMap(attacked_player, sq, kKing)
             & GetPiecesByType(kKing, attacking_player));
}

Bitboard Board::GetAttackMap(const int& attacking_player, const int& sq,
                              const int& attacking_piece) const {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::GetAttackMap()");
  }

  Bitboard attack_map = 0X0;
  int attacked_player = GetOtherPlayer(attacking_player);
  switch (attacking_piece) {
    case kPawn: {
      Bitboard capture_attacks, push_attacks;
      if (attacking_player == kWhite) {
        capture_attacks = kNonSliderAttackMaps[kWhitePawnCapture][sq];
        push_attacks = kNonSliderAttackMaps[kWhitePawnPush][sq];
      }
      else if (attacking_player == kBlack) {
        capture_attacks = kNonSliderAttackMaps[kBlackPawnCapture][sq];
        push_attacks = kNonSliderAttackMaps[kBlackPawnPush][sq];
      }
      // Include capture attacks that attack occupied squares only.
      attack_map = (capture_attacks & player_pieces_[attacked_player])
                    | push_attacks;
      break;
    }
    case kKnight:
      attack_map = kNonSliderAttackMaps[kKnightAttack][sq];
      break;
    // Use the magic bitboard method to get possible moves for bishops and
    // rooks. The Boost library's 128 bit unsigned int data type "uint128_t"
    // is used here to avoid integer overflow.
    case kBishop: {
      Bitboard all_pieces = player_pieces_[kWhite] | player_pieces_[kBlack];
      Bitboard blockers = kSliderPieceMasks[kBishopMoves][sq] & all_pieces;
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
      Bitboard blockers = kSliderPieceMasks[kRookMoves][sq] & all_pieces;
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

bool Board::CastlingLegal(const int& player, const int& board_side) const {
  if (player != kWhite && player != kBlack) {
    throw invalid_argument("player in Board::CastlingLegal()");
  }

  if (board_side == kQueenSide) {
    return castling_rights_[player][kQueenSide] && !KingInCheck(player)
           && ((player == kWhite && piece_layout_[kSqB1] == kNA
                && piece_layout_[kSqC1] == kNA
                && piece_layout_[kSqD1] == kNA
                && GetAttackersToSq(kSqD1, kWhite) == 0X0)
               || (player == kBlack && GetPieceOnSq(kSqB8) == kNA
                   && piece_layout_[kSqC8] == kNA
                   && piece_layout_[kSqD8] == kNA
                   && GetAttackersToSq(kSqD8, kBlack) == 0X0));
  } else if (board_side == kKingSide) {
    return castling_rights_[player][kKingSide] && !KingInCheck(player)
           && ((player == kWhite && piece_layout_[kSqF1] == kNA
                && GetAttackersToSq(kSqF1, kWhite) == 0X0
                && piece_layout_[kSqG1] == kNA)
               || (player == kBlack && GetPieceOnSq(kSqF8) == kNA
                   && GetAttackersToSq(kSqF8, kBlack) == 0X0
                   && piece_layout_[kSqG8] == kNA));
  } else {
    throw invalid_argument("board_side in Board::CastlingLegal()");
  }
}

bool Board::DoublePawnPushLegal(const int& player, const int& file) const {
  if (!FileOnBoard(file)) {
    throw invalid_argument("file in Board::DoublePawnPushLegal()");
  }

  if (player == kWhite) {
    int rank3_double_pawn_push_sq = GetSqFromRankFile(kRank3, file);
    int rank2_double_pawn_push_sq = GetSqFromRankFile(kRank2, file);
    return piece_layout_[rank3_double_pawn_push_sq] == kNA
           && piece_layout_[rank2_double_pawn_push_sq] == kPawn
           && player_layout_[rank2_double_pawn_push_sq] == kWhite;
  } else if (player == kBlack) {
    int rank6_double_pawn_push_sq = GetSqFromRankFile(kRank6, file);
    int rank7_double_pawn_push_sq = GetSqFromRankFile(kRank7, file);
    return piece_layout_[rank6_double_pawn_push_sq] == kNA
           && piece_layout_[rank7_double_pawn_push_sq] == kPawn
           && player_layout_[rank7_double_pawn_push_sq] == kBlack;
  } else {
    throw invalid_argument("player in Board::DoublePawnPushLegal()");
  }
}

bool Board::KingInCheck(const int& player) const {
  if (player != kWhite && player != kBlack) {
    throw invalid_argument("player in Board::KingInCheck()");
  }

  Bitboard king_board = pieces_[kKing] & player_pieces_[player];
  int king_sq = GetSqOfFirstPiece(king_board);
  return GetAttackersToSq(king_sq, player);
}

bool Board::MakeMove(const Move& move, string& err_msg) {
  // Make all non-castling moves.
  if (move.castling_type == kNA) {
    // Remove a captured piece from the board.
    if (move.captured_piece != kNA) {
      int other_player = GetOtherPlayer(move.moving_player);
      if (move.is_ep) {
        int ep_capture_sq;
        int target_file = GetFileFromSq(move.target_sq);
        // Compute the position for a captured pawn in an en passent,
        // and then remove it from the board.
        if (move.moving_player == kWhite) {
          ep_capture_sq = GetSqFromRankFile(kRank5, target_file);
          piece_layout_[ep_capture_sq] = kNA;
          player_layout_[ep_capture_sq] = kNA;
        } else if (move.moving_player == kBlack) {
          ep_capture_sq = GetSqFromRankFile(kRank4, target_file);
          piece_layout_[ep_capture_sq] = kNA;
          player_layout_[ep_capture_sq] = kNA;
        }
        Bitboard ep_capture_mask = ~(1ULL << ep_capture_sq);
        pieces_[kPawn] &= ep_capture_mask;
        player_pieces_[other_player] &= ep_capture_mask;
      } else {
        Bitboard piece_capture_mask = ~(1ULL << move.target_sq);
        pieces_[move.captured_piece] &= piece_capture_mask;
        player_pieces_[other_player] &= piece_capture_mask;
      }
    }
    MovePiece(move.moving_player, move.moving_piece, move.start_sq,
              move.target_sq, move.promoted_to_piece);
  } else if (move.castling_type == kQueenSide) {
    // Make queenside castling moves.
    if (move.moving_player == kWhite) {
      MovePiece(kWhite, kRook, kSqA1, kSqD1);
      MovePiece(kWhite, kKing, kSqE1, kSqC1);
    } else if (move.moving_player == kBlack) {
      MovePiece(kBlack, kRook, kSqA8, kSqD8);
      MovePiece(kBlack, kKing, kSqE8, kSqC8);
    }
  } else if (move.castling_type == kKingSide) {
    // Make kingside castling moves.
    if (move.moving_player == kWhite) {
      MovePiece(kWhite, kRook, kSqH1, kSqF1);
      MovePiece(kWhite, kKing, kSqE1, kSqG1);
    } else if (move.moving_player == kBlack) {
      MovePiece(kBlack, kRook, kSqH8, kSqF8);
      MovePiece(kBlack, kKing, kSqE8, kSqG8);
    }
  }

  int og_ep_target_sq = ep_target_sq_;
  ep_target_sq_ = move.new_ep_target_sq;
  // Update castling rights and keep track of which castling moves
  // have been made.
  int og_castling_right_white_queenside = castling_rights_[kWhite][kQueenSide];
  int og_castling_right_white_kingside = castling_rights_[kWhite][kKingSide];
  int og_castling_right_black_queenside = castling_rights_[kBlack][kQueenSide];
  int og_castling_right_black_kingside = castling_rights_[kBlack][kKingSide];
  if (move.castling_type != kNA) {
    castling_status_[move.moving_player][move.castling_type] = true;
    castling_rights_[move.moving_player][move.castling_type] = false;
  } else if (move.moving_piece == kKing) {
    castling_rights_[move.moving_player][kQueenSide] = false;
    castling_rights_[move.moving_player][kKingSide] = false;
  } else if (move.moving_piece == kRook) {
    int start_rank = GetRankFromSq(move.start_sq);
    int start_file = GetFileFromSq(move.start_sq);
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
  } else if (move.captured_piece == kRook) {
    // Revoke the appropriate castling rights if a player's rook is captured.
    if (move.moving_player == kWhite) {
      if (move.target_sq == kSqA8 && castling_rights_[kBlack][kQueenSide]) {
        castling_rights_[kBlack][kQueenSide] = false;
      } else if (move.target_sq == kSqH8
                 && castling_rights_[kBlack][kKingSide]) {
        castling_rights_[kBlack][kKingSide] = false;
      }
    } else if (move.moving_player == kBlack) {
      if (move.target_sq == kSqA1 && castling_rights_[kWhite][kQueenSide]) {
        castling_rights_[kWhite][kQueenSide] = false;
      } else if (move.target_sq == kSqH1
                 && castling_rights_[kWhite][kKingSide]) {
        castling_rights_[kWhite][kKingSide] = false;
      }
    }
  }
  // Reset the halfmove clock if a pawn was moved or if a move resulted in a
  // capture.
  int og_halfmove_clock = halfmove_clock_;
  if (move.captured_piece != kNA || move.moving_piece == kPawn) {
    halfmove_clock_ = 0;
  } else {
    halfmove_clock_++;
  }

  // Undo the move if it puts the king in check.
  if (KingInCheck(move.moving_player)) {
    UnmakeMove(move, og_ep_target_sq,
               og_castling_right_white_queenside,
               og_castling_right_white_kingside,
               og_castling_right_black_queenside,
               og_castling_right_black_kingside,
               og_halfmove_clock);
    err_msg = "move leaves king in check";
    return false;
  } else {
    return true;
  }
}

bool Board::MakeMove(const Move& move) {
  string dummy_err_msg;
  return MakeMove(move, dummy_err_msg);
}

int Board::GetCastlingRight(const int& player, const int& board_side) const {
  if (player != kWhite && player != kBlack) {
    throw invalid_argument("player in Board::GetCastlingRight()");
  }
  if (board_side != kQueenSide && board_side != kKingSide) {
    throw invalid_argument("board_side in Board::GetCastlingRight()");
  }

  return castling_rights_[player][board_side];
}

int Board::GetEpTargetSq() const {
  return ep_target_sq_;
}

int Board::GetHalfmoveClock() const {
  return halfmove_clock_;
}

int Board::GetPieceOnSq(const int& sq) const {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::GetPieceOnSq()");
  }

  return piece_layout_[sq];
}

int Board::GetPlayerOnSq(const int& sq) const {
  return player_layout_[sq];
}

Bitboard Board::GetPiecesByType(const int& piece_type,
                                const int& player) const {
  if (piece_type != kNA && (piece_type < kPawn || piece_type > kKing)) {
    cout << "hi!" << endl;
    throw invalid_argument("piece_type in Board::GetPiecesByType()");
  } else if (player != kNA && player != kWhite && player != kBlack) {
    throw invalid_argument("player in Board::GetPiecesByType()");
  }

  if (player == kNA && piece_type != kNA) {
    return pieces_[piece_type];
  } else if (player != kNA && piece_type == kNA) {
    return player_pieces_[player];
  } else {
    return pieces_[piece_type] & player_pieces_[player];
  }
}

// TODO: Change this to update every move instead of recomputing the hash
//       every time a move is made.
U64 Board::GetBoardHash(const int& player_to_move) const {
  if (player_to_move != kWhite && player_to_move != kBlack) {
    throw invalid_argument("player_to_move in Board::GetBoardHash()");
  }

  // Use the Zobrist Hashing algorithm to compute a unique hash of the board
  // state. This involves hashing all stored pseudo-random numbers applicable
  // to a given game position. Note that there is a small chance of collisions
  // which is mostly unavoidable.
  U64 board_hash = 0;
  for (int player = kWhite; player <= kBlack; ++player) {
    for (int board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      if(castling_status_[player][board_side]) {
        board_hash ^= castling_rights_rand_nums_[player][board_side];
      }
    }
  }
  if (ep_target_sq_ != kNA) {
    int ep_target_file = GetFileFromSq(ep_target_sq_);
    board_hash ^= ep_file_rand_nums_[ep_target_file];
  }
  int piece_type;
  for(int sq = kSqA1; sq <= kSqH8; ++sq) {
      piece_type = piece_layout_[sq];
      if (piece_type != kNA) {
        board_hash ^= piece_rand_nums_[piece_type][sq];
    }
  }
  if (player_to_move == kBlack) {
    board_hash ^= black_to_move_rand_num_;
  }
  return board_hash;
}

// Assume the passed move has been made use MakeMove(). Calling UnmakeMove()
// on a move that wasn't already made will result in undefined behavior.
void Board::UnmakeMove(const Move& move, int og_ep_target_sq,
                       int og_castling_right_white_queenside,
                       int og_castling_right_white_kingside,
                       int og_castling_right_black_queenside,
                       int og_castling_right_black_kingside,
                       int og_halfmove_clock) {
  // Undo all non-castling moves.
  if (move.castling_type == kNA) {
    int ep_capture_sq;
    int other_player = GetOtherPlayer(move.moving_player);
    // Move the moving piece back to its original position and undo
    // any pawn promotion.
    if (move.promoted_to_piece == kNA) {
      MovePiece(move.moving_player, move.moving_piece, move.target_sq,
                move.start_sq);
    } else {
      // Remove the promoted-to piece from the board.
      Bitboard piece_promotion_rm_mask = ~(1ULL << move.target_sq);
      pieces_[move.promoted_to_piece] &= piece_promotion_rm_mask;
      player_pieces_[move.moving_player] &= piece_promotion_rm_mask;
      piece_layout_[move.target_sq] = kNA;
      player_layout_[move.target_sq] = kNA;

      // Add the oginal pawn back to its start position.
      Bitboard og_piece_pos_mask = 1ULL << move.start_sq;
      pieces_[kPawn] |= og_piece_pos_mask;
      player_pieces_[move.moving_player] |= og_piece_pos_mask;
      piece_layout_[move.start_sq] = kPawn;
      player_layout_[move.start_sq] = move.moving_player;
    }
    // Place a captured piece back onto the board.
    if (move.captured_piece != kNA) {
      if (move.is_ep) {
        int target_file = GetFileFromSq(move.target_sq);
        // Place a captured pawn back onto the board after an en passent.
        if (move.moving_player == kWhite) {
          ep_capture_sq = GetSqFromRankFile(kRank5, target_file);
          piece_layout_[ep_capture_sq] = kPawn;
          player_layout_[ep_capture_sq] = kBlack;
        } else if (move.moving_player == kBlack) {
          ep_capture_sq = GetSqFromRankFile(kRank4, target_file);
          piece_layout_[ep_capture_sq] = kPawn;
          player_layout_[ep_capture_sq] = kWhite;
        }
        Bitboard undo_ep_capture_mask = 1ULL << ep_capture_sq;
        pieces_[kPawn] |= undo_ep_capture_mask;
        player_pieces_[other_player] |= undo_ep_capture_mask;
      } else {
        Bitboard undo_capture_mask = 1ULL << move.target_sq;
        pieces_[move.captured_piece] |= undo_capture_mask;
        player_pieces_[other_player] |= undo_capture_mask;
      }
    }
  // Undo queenside castling moves.
  } else if (move.castling_type == kQueenSide) {
    if (move.moving_player == kWhite) {
      MovePiece(kWhite, kRook, kSqD1, kSqA1);
      MovePiece(kWhite, kKing, kSqC1, kSqE1);
    } else if (move.moving_player == kBlack) {
      MovePiece(kBlack, kRook, kSqD8, kSqA8);
      MovePiece(kBlack, kKing, kSqC8, kSqE8);
    }
  // Undo kingside castling moves.
  } else if (move.castling_type == kKingSide) {
    if (move.moving_player == kWhite) {
      MovePiece(kWhite, kRook, kSqF1, kSqH1);
      MovePiece(kWhite, kKing, kSqG1, kSqE1);
    } else if (move.moving_player == kBlack) {
      MovePiece(kBlack, kRook, kSqF8, kSqH8);
      MovePiece(kBlack, kKing, kSqG8, kSqE8);
    }
  }
  // Reset castling rights, which castling moves have been made,
  // and the halfmove clock.
  ep_target_sq_ = og_ep_target_sq;
  if (move.castling_type != kNA) {
    castling_status_[move.moving_player][move.castling_type] = false;
  }
  castling_rights_[kWhite][kQueenSide] = og_castling_right_white_queenside;
  castling_rights_[kWhite][kKingSide] = og_castling_right_white_kingside;
  castling_rights_[kBlack][kQueenSide] = og_castling_right_black_queenside;
  castling_rights_[kWhite][kKingSide] = og_castling_right_black_kingside;
  halfmove_clock_ = og_halfmove_clock;
}

void Board::AddPiece(const int& piece_type, const int& player,
                     const int& sq) {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::AddPiece()");
  }

  if (piece_type == kNA && player == kNA) {
    piece_layout_[sq] = kNA;
    player_layout_[sq] = kNA;
  } else if (piece_type >= kPawn && piece_type <= kKing
             && (player == kWhite || player == kBlack)) {
    Bitboard piece_mask = 1ULL << sq;
    pieces_[piece_type] |= piece_mask;
    player_pieces_[player] |= piece_mask;
    piece_layout_[sq] = piece_type;
    player_layout_[sq] = player;
  } else {
    throw invalid_argument("piece_type, player in Board::AddPiece()");
  }
}

void Board::CopyBoard(const Board& src) {
  for (int piece = kPawn; piece <= kKing; ++piece) {
    pieces_[piece] = src.pieces_[piece];

    for (int sq = kSqA1; sq <= kSqH8; ++sq) {
      piece_rand_nums_[piece][sq] = src.piece_rand_nums_[piece][sq];
    }
  }

  for (int player = kWhite; player <= kBlack; ++player) {
    player_pieces_[player] = src.player_pieces_[player];

    for (int board_side = kQueenSide; board_side <= kKingSide; ++board_side) {
      castling_rights_[player][board_side] = 
        src.castling_rights_[player][board_side];
      castling_status_[player][board_side] = 
        src.castling_rights_[player][board_side];
      
      castling_rights_rand_nums_[player][board_side] = 
        src.castling_rights_rand_nums_[player][board_side];
    }
  }

  ep_target_sq_ = src.ep_target_sq_;
  halfmove_clock_ = src.halfmove_clock_;
  for (int sq = kSqA1; sq <= kSqH8; ++sq) {
    piece_layout_[sq] = src.piece_layout_[sq];
    player_layout_[sq] = src.player_layout_[sq];
  }

  for (int file = kFileA; file <= kFileH; ++file) {
    ep_file_rand_nums_[file] = src.ep_file_rand_nums_[file];
  }
  black_to_move_rand_num_ = src.black_to_move_rand_num_;
}

void Board::MovePiece(const int& moving_player, const int& piece,
                      const int& start_sq, const int& target_sq,
                      const int& promoted_to_piece) {
  if (moving_player != kWhite && moving_player != kBlack) {
    throw invalid_argument("moving_player in Board::MovePiece()");
  } else if (piece < kPawn || piece > kKing) {
    throw invalid_argument("piece in Board::MovePiece()");
  } else if (!SqOnBoard(start_sq)) {
    throw invalid_argument("start_sq in Board::MovePiece()");
  } else if (!SqOnBoard(target_sq)) {
    throw invalid_argument("target_sq in Board::MovePiece()");
  } else if (promoted_to_piece != kNA
             && (promoted_to_piece <= kPawn || promoted_to_piece >= kKing)) {
    throw invalid_argument("promoted_to_piece in Board::MovePiece()");
  }

  // Remove the selected piece from its start position on the board.
  piece_layout_[start_sq] = kNA;
  player_layout_[start_sq] = kNA;
  Bitboard rm_piece_mask = ~(1ULL << start_sq);
  pieces_[piece] &= rm_piece_mask;
  player_pieces_[moving_player] &= rm_piece_mask;

  // Add the selected piece back at its end position on the board.
  Bitboard new_piece_pos_mask = 1ULL << target_sq;
  if (promoted_to_piece == kNA) {
    pieces_[piece] |= new_piece_pos_mask;
    piece_layout_[target_sq] = piece;
  // Add a piece back as the type it promotes to if move is a pawn promotion.
  } else {
    pieces_[promoted_to_piece] |= new_piece_pos_mask;
    piece_layout_[target_sq] = promoted_to_piece;
  }
  player_layout_[target_sq] = moving_player;
  player_pieces_[moving_player] |= new_piece_pos_mask;
}

bool RankOnBoard(const int& rank) {
  return rank >= kRank1 && rank <= kRank8;
}
bool FileOnBoard(const int& file) {
  return file >= kFileA && file <= kFileH;
}
bool SqOnBoard(const int& sq) {
  return sq >= kSqA1 && sq <= kSqH8;
}

int GetOtherPlayer(const int& player) {
  if (player == kWhite) {
    return kBlack;
  } else if (player == kBlack) {
    return kWhite;
  } else {
    throw invalid_argument("player in GetOtherPlayer()");
  }
}
int GetFileFromSq(const int& sq) {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in GetFileFromSq()");
  }

  return sq & 7;
}
int GetRankFromSq(const int& sq) {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in GetRankFromSq()");
  }

  return sq >> 3;
}
int GetSqFromRankFile(const int& rank, const int& file) {
  if (!RankOnBoard(rank)) {
    throw invalid_argument("rank in GetSqFromRankFile()");
  }
  if (!FileOnBoard(file)) {
    throw invalid_argument("file in GetFileFromSq()");
  }

  return rank * kNumFiles + file;
}
int GetSqOfFirstPiece(const Bitboard& board) {
  if (board == 0X0) {
    throw invalid_argument("board in GetSqOfFirstPiece()");
  }

  int bitscan_index = static_cast<int>(
    ((board & -board) * kDebruijn64bitSeq) >> 58
  );
  return kBitscanForwardLookupTable[bitscan_index];
}
