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
#include <string>
#include <unordered_map>

#include "move.h"

typedef uint64_t Bitboard;
typedef uint64_t U64;

enum BoardSide {
  kQueenSide, kKingSide
};
enum File {
  kFileA, kFileB, kFileC, kFileD, kFileE, kFileF, kFileG, kFileH,
};
enum NonSliderAttackMap {
  kWhitePawnPush, kWhitePawnCapture, kBlackPawnPush,
  kBlackPawnCapture, kKnightAttack, kKingAttack,
};
enum Rank {
  kRank1, kRank2, kRank3, kRank4, kRank5, kRank6, kRank7, kRank8,
};
enum SliderPieceMask {
  kBishopMoves, kRookMoves,
};
// Specify the squares necessary to perform castling moves.
enum Square {
  kSqA1 = 0, kSqB1 = 1, kSqC1 = 2, kSqD1 = 3, kSqE1 = 4, kSqF1 = 5, kSqG1 = 6,
  kSqH1 = 7, kSqA8 = 56, kSqB8 = 57, kSqC8 = 58, kSqD8 = 59, kSqE8 = 60,
  kSqF8 = 61, kSqG8 = 62, kSqH8 = 63,
};
enum Piece {
  kPawn, kKnight, kBishop, kRook, kQueen, kKing,
};
enum Player {
  kWhite, kBlack,
};

const int kNumBoardSides = 2;
const int kNumFiles = 8;
const int kNumNonSliderMasks = 6;
const int kNumPieceTypes = 6;
const int kNumPlayers = 2;
const int kNumRanks = 8;
const int kNumSliderMasks = 2;
const int kNumSq = 64;

const U64 kDebruijn64bitSeq = 0x03F79D71B4CB0A89ULL;

const Bitboard kFileMasks[kNumFiles] = {
  0X0101010101010101, 0X0202020202020202,
  0X0404040404040404, 0X0808080808080808,
  0X1010101010101010, 0X2020202020202020,
  0X4040404040404040, 0X8080808080808080
};
const Bitboard kRankMasks[kNumRanks] = {
  0X00000000000000FF, 0X000000000000FF00,
  0X0000000000FF0000, 0X00000000FF000000,
  0X000000FF00000000, 0X0000FF0000000000,
  0X00FF000000000000, 0XFF00000000000000
};

const int kBitscanForwardLookupTable[64] = {
    0,  1, 48,  2, 57, 49, 28,  3,
   61, 58, 50, 42, 38, 29, 17,  4,
   62, 55, 59, 36, 53, 51, 43, 22,
   45, 39, 33, 30, 24, 18, 12,  5,
   63, 47, 56, 27, 60, 41, 37, 16,
   54, 35, 52, 21, 44, 32, 23, 11,
   46, 26, 40, 15, 34, 20, 31, 10,
   25, 14, 19,  9, 13,  8,  7,  6
};
// Store the length (in bits) of magic numbers for move generation.
const int kBishopMagicLengths[kNumSq] = {
  6, 5, 5, 5, 5, 5, 5, 6,
  5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 7, 7, 7, 7, 5, 5,
  5, 5, 7, 9, 9, 7, 5, 5,
  5, 5, 7, 9, 9, 7, 5, 5,
  5, 5, 7, 7, 7, 7, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5,
  6, 5, 5, 5, 5, 5, 5, 6
};
const int kRookMagicLengths[kNumSq] = {
  12, 11, 11, 11, 11, 11, 11, 12,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  11, 10, 10, 10, 10, 10, 10, 11,
  12, 11, 11, 11, 11, 11, 11, 12
};

extern const Bitboard kNonSliderAttackMaps[kNumNonSliderMasks][kNumSq];
// Store all positions bishop and rook pieces can move to on an empty board,
// excluding endpoints.
extern const Bitboard kSliderPieceMasks[kNumSliderMasks][kNumSq];
// Store all positions bishop and rook pieces can move to on an empty board,
// including endpoints.
extern const Bitboard kUnblockedSliderAttackMaps[kNumSliderMasks][kNumSq];

extern const U64 kMagics[kNumSliderMasks][kNumSq];

extern const std::unordered_map<U64, Bitboard> kMagicIndexToAttackMap;

bool RankOnBoard(const int& rank);
bool FileOnBoard(const int& file);
bool SqOnBoard(const int& sq);

int GetOtherPlayer(const int& player);
int GetFileFromSq(const int& sq);
int GetRankFromSq(const int& sq);
int GetSqFromRankFile(const int& rank, const int& file);
// A Bitscan Forward function based on Kim Walisch's implementation
// of the De Bruijn Bitscan Routine.
int GetSqOfFirstPiece(const Bitboard& board);

class Board {
public:
  Board(int& player_to_move, const std::string& init_pos);
  Board(const Board& src);

  Board& operator=(const Board& src);

  // Return possible attacks a specified piece can make on all other pieces.
  Bitboard GetAttackMap(const int& attacking_player, const int& sq,
                        const int& attacking_piece) const;
  Bitboard GetPiecesByType(const int& piece_type, const int& player) const;

  bool CastlingLegal(const int& player, const int& board_side) const;
  bool DoublePawnPushLegal(const int& player, const int& file) const;
  bool KingInCheck(const int& player) const;
  // Return false if the move was found to put the moving player's King in
  // check. Return true is the board state was succesfully updated.
  bool MakeMove(const Move& move, std::string& err_msg);
  bool MakeMove(const Move& move);

  int GetCastlingRight(const int& player, const int& board_side) const;
  int GetEpTargetSq() const;
  int GetHalfmoveClock() const;
  int GetPieceOnSq(const int& sq) const;
  int GetPlayerOnSq(const int& sq) const;

  // Return an (almost) unique hash that represents the current board state.
  U64 GetBoardHash(const int& player_to_move) const;

  // Unmake the given move, assuming it was already made with MakeMove().
  void UnmakeMove(const Move& move, int og_ep_target_sq,
                  int og_castling_right_white_queenside,
                  int og_castling_right_white_kingside,
                  int og_castling_right_black_queenside,
                  int og_castling_right_black_kingside,
                  int og_halfmove_clock);
private:
  Bitboard GetAttackersToSq(const int& sq, const int& attacked_player) const;

  void AddPiece(const int& piece_type, const int& player, const int& sq);
  void CopyBoard(const Board& src);
  void MovePiece(const int& moving_player, const int& piece,
                 const int& start_sq, const int& target_sq,
                 const int& promoted_to_piece = kNA);

  // Store bitboard board representations of each type
  // of piece that are still active in the game.
  Bitboard pieces_[kNumPieceTypes];
  // Store bitboard board representations of the pieces
  // in each players' posession.
  Bitboard player_pieces_[kNumPlayers];

  bool castling_rights_[kNumPlayers][kNumBoardSides];
  // Store which castling moves have been played so far.
  bool castling_status_[kNumPlayers][kNumBoardSides];

  // Keep track of the square (if it exists) an en passent move is elligible
  // to land on during a given turn.
  int ep_target_sq_;
  // Keep track of the number of moves since a pawn movement or capture to
  // enfore the Fifty Move Rule.
  int halfmove_clock_;
  // Store an 8x8 board representation.
  int piece_layout_[kNumSq];
  int player_layout_[kNumSq];

  // Store a set of pseudo-random numbers for Zobrist Hashing.
  U64 castling_rights_rand_nums_[kNumPlayers][kNumBoardSides];
  U64 ep_file_rand_nums_[kNumFiles];
  U64 piece_rand_nums_[kNumPieceTypes][kNumSq];
  U64 black_to_move_rand_num_;
};

#endif // OMEGAZERO_SRC_BOARD_H_
