/* Noah Himed
 *
 * Define a Board object, which includes both bitboard and 8x8 board
 * representations to store piece locations.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_BOARD_H_
#define OMEGAZERO_SRC_BOARD_H_

#include <cstdint>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "move.h"
#include "pawn_table.h"

namespace omegazero {

using std::invalid_argument;
using std::stack;

typedef uint64_t Bitboard;
typedef uint64_t U64;

enum BoardSide : S8 { kQueenSide, kKingSide };
enum File : S8 {
  kFileA,
  kFileB,
  kFileC,
  kFileD,
  kFileE,
  kFileF,
  kFileG,
  kFileH,
};
enum NonSliderAttackMapIndex : S8 {
  kWhitePawnPush,
  kWhitePawnCapture,
  kBlackPawnPush,
  kBlackPawnCapture,
  kKnightAttack,
  kKingAttack,
};
enum Rank : S8 {
  kRank1,
  kRank2,
  kRank3,
  kRank4,
  kRank5,
  kRank6,
  kRank7,
  kRank8,
};
enum SliderPieceMapIndex : S8 {
  kBishopMoves,
  kRookMoves,
};
// Specify the squares necessary to perform castling moves.
enum Square : S8 {
  kSqA1 = 0,
  kSqB1 = 1,
  kSqC1 = 2,
  kSqD1 = 3,
  kSqE1 = 4,
  kSqF1 = 5,
  kSqG1 = 6,
  kSqH1 = 7,
  kSqA8 = 56,
  kSqB8 = 57,
  kSqC8 = 58,
  kSqD8 = 59,
  kSqE8 = 60,
  kSqF8 = 61,
  kSqG8 = 62,
  kSqH8 = 63,
};
enum Piece : S8 {
  kPawn,
  kKnight,
  kBishop,
  kRook,
  kQueen,
  kKing,
};
enum Player : S8 {
  kWhite,
  kBlack,
};

constexpr S8 kNumBoardSides = 2;
constexpr S8 kNumFiles = 8;
constexpr S8 kNumNonSliderMaps = 6;
constexpr S8 kNumPieceTypes = 6;
constexpr S8 kNumPlayers = 2;
constexpr S8 kNumRanks = 8;
constexpr S8 kNumSliderMaps = 2;
constexpr S8 kNumSq = 64;

// Store piece values expressed in centipawns for evaluation function. Piece
// order in array is pawn, knight, bishop, rook, queen, king.
constexpr int kPieceVals[kNumPieceTypes] = {100, 320, 330, 500, 900, 20000};

constexpr Bitboard kFileMasks[kNumFiles] = {
    0X0101010101010101, 0X0202020202020202, 0X0404040404040404,
    0X0808080808080808, 0X1010101010101010, 0X2020202020202020,
    0X4040404040404040, 0X8080808080808080};
constexpr Bitboard kRankMasks[kNumRanks] = {
    0X00000000000000FF, 0X000000000000FF00, 0X0000000000FF0000,
    0X00000000FF000000, 0X000000FF00000000, 0X0000FF0000000000,
    0X00FF000000000000, 0XFF00000000000000};

constexpr S8 kBitscanForwardLookupTable[64] = {
    0,  1,  48, 2,  57, 49, 28, 3,  61, 58, 50, 42, 38, 29, 17, 4,
    62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12, 5,
    63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11,
    46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6};
// Store the length (in bits) of magic numbers for move generation.
constexpr S8 kBishopMagicLengths[kNumSq] = {
    6, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 7, 7,
    5, 5, 5, 5, 7, 9, 9, 7, 5, 5, 5, 5, 7, 9, 9, 7, 5, 5, 5, 5, 7, 7,
    7, 7, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 6};
constexpr S8 kRookMagicLengths[kNumSq] = {
    12, 11, 11, 11, 11, 11, 11, 12, 11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11, 12, 11, 11, 11, 11, 11, 11, 12};

extern const Bitboard kNonSliderAttackMaps[kNumNonSliderMaps][kNumSq];
// Store all positions bishop and rook pieces can move to on an empty board,
// excluding endpoints.
extern const Bitboard kSliderPieceMaps[kNumSliderMaps][kNumSq];
// Store all positions bishop and rook pieces can move to on an empty board,
// including endpoints.
extern const Bitboard kUnblockedSliderAttackMaps[kNumSliderMaps][kNumSq];
extern const Bitboard kPawnFrontAttackspanMasks[kNumPlayers][kNumSq];
extern const Bitboard kPawnFrontSpanMasks[kNumPlayers][kNumSq];

extern const int kPieceSqTable[kNumPieceTypes][kNumSq];
extern const int kEndgameKingPieceSqTable[kNumSq];

extern const U64 kMagics[kNumSliderMaps][kNumSq];

extern const std::unordered_map<U64, Bitboard> kMagicIndexToAttackMap;

auto MultipleSetSq(Bitboard board) -> bool;
auto OneSqSet(Bitboard board) -> bool;
auto RankOnBoard(S8 rank) -> bool;
auto FileOnBoard(S8 file) -> bool;
auto SqOnBoard(S8 sq) -> bool;

auto GetOtherPlayer(S8 player) -> S8;
auto GetNumSetSq(Bitboard board) -> S8;
auto GetFileFromSq(S8 sq) -> S8;
auto GetRankFromSq(S8 sq) -> S8;
auto GetSqFromRankFile(S8 rank, S8 file) -> S8;
auto GetSqOfFirstPiece(Bitboard board) -> S8;

// Clear the least significant bit set of the passed in bitboard.
auto RemoveFirstSq(Bitboard& board) -> void;

class Board {
 public:
  Board(const std::string& init_pos);

  auto operator==(const Board& rhs) const -> bool;

  // Return possible attacks a specified piece can make on all other pieces.
  auto GetAttackMap(S8 attacking_player, S8 sq, S8 attacking_piece) const
      -> Bitboard;
  auto GetPiecesByType(S8 piece_type, S8 player) const -> Bitboard;

  auto CastlingLegal(S8 board_side) const -> bool;
  auto DoublePawnPushLegal(S8 file) const -> bool;
  auto KingInCheck() const -> bool;

  // Compute and return a static evaluation of the board state. This score is
  // relative to the side being evaluated and symmetric, as required by the
  // Negamax Algorithm.
  auto Evaluate() -> int;

  auto GetEpTargetSq() const -> S8;
  auto GetHalfmoveClock() const -> S8;
  auto GetPieceOnSq(S8 sq) const -> S8;
  auto GetPlayerOnSq(S8 sq) const -> S8;
  auto GetPlayerToMove() const -> S8;

  // Return an (almost) unique hash that represents the current board state.
  auto GetBoardHash() const -> U64;

  auto ClearPawnTable() -> void;
  // Resets information edited during search after a search is interrupted
  // during iterative deepening.
  // WARNING: Calling this function without first calling SavePos() will cause
  // undefined behavior.
  auto ResetPos() -> void;
  // Caches a copy of information edited during search before iterative
  // deepening, allowing ResetPos() to be called after iterative deepening.
  auto SavePos() -> void;
  auto SwitchPlayer() -> void;
  auto MakeMove(const Move& move) -> void;
  auto MakeNullMove() -> void;
  // Unmake the given move, assuming it was already made with MakeMove(). Note
  // that this function does not flip the player to move variable.
  // WARNING: Calling this function without first calling MakeMove() with the
  // same move param will cause undefined behavior.
  auto UnmakeMove(const Move& move) -> void;
  // WARNING: Calling this function without first calling MakeNullMove() will
  // cause undefined behavior.
  auto UnmakeNullMove() -> void;

 private:
  auto GetAttackersToSq(S8 sq, S8 attacked_player) const -> Bitboard;

  // Weighs material balance and positional bonuses and computes the white and
  // black pawn cummulative front attackspans for evaluating pawn structure.
  auto EvaluatePiecePositions(Bitboard& white_attackspan,
                              Bitboard& white_attack_map,
                              Bitboard& white_defender_map,
                              Bitboard& black_attackspan,
                              Bitboard& black_attack_map,
                              Bitboard& black_defender_map) const -> int;
  auto EvaluatePawnStructure(Bitboard white_attackspan,
                             Bitboard white_attack_map,
                             Bitboard white_defender_map,
                             Bitboard black_attackspan,
                             Bitboard black_attack_map,
                             Bitboard black_defender_map) const -> int;

  // Get a hash of the current pawn structure;
  auto GetPawnHash() const -> U64;

  auto AddPiece(S8 piece_type, S8 player, S8 sq) -> void;
  auto InitHash() -> void;
  // Parse a FEN string to initialize the board state.
  auto InitBoardPos(const std::string& init_pos) -> void;
  auto MakeNonCastlingMove(const Move& move) -> void;
  auto MovePiece(S8 piece, S8 start_sq, S8 target_sq,
                 S8 promoted_to_piece = kNA) -> void;
  auto UnmakeNonCastlingMove(const Move& move) -> void;
  auto UpdateCastlingRights(const Move& move) -> void;

  // Store a copy of all information edited during seach to revert back to after
  // a search is interrupted.
  struct {
    Bitboard pieces[kNumPieceTypes];
    Bitboard player_pieces[kNumPlayers];

    bool castling_rights[kNumPlayers][kNumBoardSides];
    bool castling_status[kNumPlayers];

    S8 ep_target_sq;
    S8 halfmove_clock;
    S8 piece_layout[kNumSq];
    S8 player_layout[kNumSq];
    S8 player_to_move;

    stack<bool> white_queenside_castling_rights_history;
    stack<bool> white_kingside_castling_rights_history;
    stack<bool> black_queenside_castling_rights_history;
    stack<bool> black_kingside_castling_rights_history;
    stack<S8> ep_target_sq_history;
    stack<S8> halfmove_clock_history;

    U64 board_hash;
    U64 pawn_hash;
  } saved_pos_info_;

  // Store bitboard board representations of each type
  // of piece that are still active in the game.
  Bitboard pieces_[kNumPieceTypes];
  // Store bitboard board representations of the pieces
  // in each players' posession.
  Bitboard player_pieces_[kNumPlayers];

  bool castling_rights_[kNumPlayers][kNumBoardSides];
  bool castling_status_[kNumPlayers];

  PawnTable pawn_table_;

  // Keep track of the square (if it exists) an en passent move is elligible
  // to land on during a given turn.
  S8 ep_target_sq_;
  // Keep track of the number of moves since a pawn movement or capture to
  // enfore the Fifty Move Rule.
  S8 halfmove_clock_;
  // Store an 8x8 board representation.
  S8 piece_layout_[kNumSq];
  S8 player_layout_[kNumSq];
  S8 player_to_move_;

  // Store a history of irreversible position aspects for UnmakeMove().
  stack<bool> white_queenside_castling_rights_history_;
  stack<bool> white_kingside_castling_rights_history_;
  stack<bool> black_queenside_castling_rights_history_;
  stack<bool> black_kingside_castling_rights_history_;
  stack<S8> ep_target_sq_history_;
  stack<S8> halfmove_clock_history_;

  // Store a set of pseudo-random numbers for Zobrist Hashing.
  U64 board_hash_;
  U64 pawn_hash_;
  U64 castling_rights_rand_nums_[kNumPlayers][kNumBoardSides];
  U64 ep_file_rand_nums_[kNumFiles];
  U64 piece_rand_nums_[kNumPieceTypes][kNumSq];
  U64 black_to_move_rand_num_;
};

// Implement public inline non-member functions.

inline auto MultipleSetSq(Bitboard board) -> bool {
  return static_cast<bool>(board & (board - 1));
}

inline auto OneSqSet(Bitboard board) -> bool {
  return board && !static_cast<bool>(board & (board - 1));
}

inline auto RankOnBoard(S8 rank) -> bool {
  return rank >= kRank1 && rank <= kRank8;
}

inline auto FileOnBoard(S8 file) -> bool {
  return file >= kFileA && file <= kFileH;
}

inline auto SqOnBoard(S8 sq) -> bool { return sq >= kSqA1 && sq <= kSqH8; }

inline auto GetOtherPlayer(S8 player) -> S8 {
  if (player == kWhite) {
    return kBlack;
  }
  if (player == kBlack) {
    return kWhite;
  }

  throw invalid_argument("player in GetOtherPlayer()");
}

inline auto GetNumSetSq(Bitboard board) -> S8 {
  constexpr U64 kOddBitsMask = 0X5555555555555555ULL;
  board = board - ((board >> 1) & kOddBitsMask);
  constexpr U64 kDuoCountMask = 0X3333333333333333ULL;
  board = (board & kDuoCountMask) + ((board >> 2) & kDuoCountMask);
  constexpr U64 kBitSumMask = 0X0F0F0F0F0F0F0F0FULL;
  board = (board + (board >> 4)) & kBitSumMask;
  constexpr U64 kDigitSumMask = 0X0101010101010101ULL;
  board = (board * kDigitSumMask) >> 56;

  return static_cast<S8>(board);
}

inline auto GetFileFromSq(S8 sq) -> S8 {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in GetFileFromSq()");
  }

  constexpr S8 kFileMask = 7;
  return sq & kFileMask;
}

inline auto GetRankFromSq(S8 sq) -> S8 {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in GetRankFromSq()");
  }

  constexpr S8 kSquareRightShiftAmt = 3;
  return static_cast<S8>(sq >> kSquareRightShiftAmt);
}

inline auto GetSqFromRankFile(S8 rank, S8 file) -> S8 {
  if (!RankOnBoard(rank)) {
    throw invalid_argument("rank in GetSqFromRankFile()");
  }
  if (!FileOnBoard(file)) {
    throw invalid_argument("file in GetFileFromSq()");
  }

  return static_cast<S8>(rank * kNumFiles + file);
}

inline auto GetSqOfFirstPiece(Bitboard board) -> S8 {
  if (board == 0X0) {
    throw invalid_argument("board in GetSqOfFirstPiece()");
  }

  // Use a Bitscan Forward algoirthm based on Kim Walisch's implementation
  // of the De Bruijn Bitscan Routine.
  constexpr U64 kDebruijn64bitSeq = 0X03F79D71B4CB0A89ULL;
  constexpr S8 kDebruijn64bitSeqRightShiftAmt = 58;
  S8 bitscan_index = static_cast<S8>(((board & -board) * kDebruijn64bitSeq) >>
                                     kDebruijn64bitSeqRightShiftAmt);
  return kBitscanForwardLookupTable[bitscan_index];
}

inline auto RemoveFirstPiece(Bitboard& board) -> void { board &= (board - 1); }

// Implement inline member functions.

inline auto Board::operator==(const Board& rhs) const -> bool {
  return GetBoardHash() == rhs.GetBoardHash();
}

inline auto Board::KingInCheck() const -> bool {
  Bitboard king_board = pieces_[kKing] & player_pieces_[player_to_move_];
  S8 king_sq = GetSqOfFirstPiece(king_board);
  return static_cast<bool>(GetAttackersToSq(king_sq, player_to_move_));
}

inline auto Board::GetEpTargetSq() const -> S8 { return ep_target_sq_; }

inline auto Board::GetHalfmoveClock() const -> S8 { return halfmove_clock_; }

inline auto Board::GetPieceOnSq(S8 sq) const -> S8 {
  if (!SqOnBoard(sq)) {
    throw invalid_argument("sq in Board::GetPieceOnSq()");
  }

  return piece_layout_[sq];
}

inline auto Board::GetPlayerOnSq(S8 sq) const -> S8 {
  return player_layout_[sq];
}

inline auto Board::GetPlayerToMove() const -> S8 { return player_to_move_; }

inline auto Board::GetBoardHash() const -> U64 { return board_hash_; }

inline auto Board::ClearPawnTable() -> void { pawn_table_.Clear(); }

inline auto Board::SwitchPlayer() -> void {
  player_to_move_ = (player_to_move_ == kWhite) ? kBlack : kWhite;
  // Update the board hash to reflect player turnover.
  board_hash_ ^= black_to_move_rand_num_;
}

inline auto Board::GetPawnHash() const -> U64 { return pawn_hash_; }

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_BOARD_H_
