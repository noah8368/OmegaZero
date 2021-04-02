// Noah Himed
// 18 March 2021
//
// Implement the Game type.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "game.h"

#include <iostream>
#include <string.h>

#include "board.h"
#include "constants.h"
#include "move.h"

#include <iomanip>

using namespace std;

Game::Game() {
  game_active_ = true;

  piece_symbols_ = {
    {make_pair(kNA, kNA), "."},
    {make_pair(kWhite, kPawn), "♙"},
    {make_pair(kWhite, kKnight), "♘"},
    {make_pair(kWhite, kBishop), "♗"},
    {make_pair(kWhite, kRook), "♖"},
    {make_pair(kWhite, kQueen), "♕"},
    {make_pair(kWhite, kKing), "♔"},
    {make_pair(kBlack, kPawn), "♟"},
    {make_pair(kBlack, kKnight), "♞"},
    {make_pair(kBlack, kBishop), "♝"},
    {make_pair(kBlack, kRook), "♜"},
    {make_pair(kBlack, kQueen), "♛"},
    {make_pair(kBlack, kKing), "♚"}
  };
  int player_in_check_ = kNA;
  int winner_ = kNA;
}

void Game::OutputGameResolution() const {
  DisplayBoard();

  if (winner_ == kNA) {
    cout << "Game has ended in a stalemate" << endl;
  } else {
    string winner_name = (winner_ == kWhite) ? "WHITE" : "BLACK";
    cout << winner_name << " wins" << endl;
  }
}

void Game::Play(int player) {
  DisplayBoard();

  string player_name = (player == kWhite) ? "WHITE" : "BLACK";
  cout << "\n\n" << player_name << " to move" << endl;

  bool move_legal;
  Move user_move;
  string err_msg, user_cmd;
  do {
    cout << "Enter move: ";
    getline(cin, user_cmd);
    move_legal = CheckMove(user_cmd, user_move, err_msg, player);
    if (move_legal) {
      if (game_active_) {
        move_legal = MakeMove(player, user_move, err_msg);
      } else {
        break;
      }
    }
    if (!move_legal) {
      cout << "ERROR: " << err_msg << endl;
    }
  } while (!move_legal);
  cout << "\n\n";

  CheckGameStatus();
}

bool Game::IsActive() const {
  return game_active_;
}

bool Game::CheckMove(string user_cmd, Move& move,
                     string& err_msg, int player) {
  err_msg = "bad command formatting";
  size_t cmd_len = user_cmd.length();
  if (cmd_len == 0) {
    return false;
  }

  // Handle the case in which the user resigns.
  if (user_cmd == "r") {
    game_active_ = false;
    winner_ = player ^ 1;
    return true;
  }

  Move entered_move;
  if (user_cmd == "0-0") {
    entered_move.castling_type = kKingSide;
    return true;
  }
  if (user_cmd == "0-0-0") {
    entered_move.castling_type = kQueenSide;
    return true;
  }

  Piece piece_type;
  char first_ch = user_cmd[0];
  switch (first_ch) {
    case 'N':
      piece_type = kKnight;
      break;
    case 'B':
      piece_type = kBishop;
      break;
    case 'R':
      piece_type = kRook;
      break;
    case 'Q':
      piece_type = kQueen;
      break;
    case 'K':
      piece_type = kKing;
      break;
    default:
      piece_type = kPawn;
      int file = first_ch - 'a';
      if (file < kA || file > kH) {
        return false;
      }
  }

  bool capture_indicated = false;
  bool is_ep = false;
  int dest_rank, dest_file;
  int origin_rank = kNA;
  int origin_file = kNA;
  int promoted_piece = kNA;
  switch (cmd_len) {
    // Handle the case of unambiguous pawn move without capture (ex: e4).
    case 2:
      dest_file = user_cmd[0] - 'a';
      dest_rank = user_cmd[1] - '1';
      // Declare invalid if a pawn move to rank 8 is indicated without
      // choosing a piece type to promote to.
      if (dest_rank == k8) {
        return false;
      }
      break;
    // Handle the cases of unambiguous non-pawn moves without capture (ex: Qe4)
    // and unambiguous pawn move and promotion (ex: d8Q).
    case 3:
      if (piece_type != kPawn) {
        dest_file = user_cmd[1] - 'a';
        dest_rank = user_cmd[2] - '1';
      } else {
        dest_rank = user_cmd[1] - '1';
        if (dest_rank != k8) {
          return false;
        }
        dest_file = user_cmd[0] - 'a';
        char promoted_piece_ch = user_cmd[2];
        switch (promoted_piece_ch) {
          case 'N':
            promoted_piece = kKnight;
            break;
          case 'B':
            promoted_piece = kBishop;
            break;
          case 'R':
            promoted_piece = kRook;
            break;
          case 'Q':
            promoted_piece = kQueen;
            break;
          default:
            return false;
        }
      }
      break;
    // Handle the cases of unambiguous captures (ex: exd6, Nxe4) and
    // ambiguous moves requiring a specified origin rank or file
    // (ex: R1a3, Rdf8).
    case 4:
      dest_file = user_cmd[2] - 'a';
      dest_rank = user_cmd[3] - '1';
      if (piece_type == kPawn) {
        if (user_cmd[1] != 'x') {
          return false;
        }
        capture_indicated = true;
        origin_file = user_cmd[0] - 'a';
      } else {
        char second_ch = user_cmd[1];
        if (second_ch - '1' >= k1 && second_ch - '1' <= k8) {
          origin_rank = second_ch - '1';
        } else if (second_ch - 'a' >= kA && second_ch - 'a' <= kH) {
          origin_file = second_ch - 'a';
        } else if (second_ch == 'x') {
          capture_indicated = true;
        } else {
          return false;
        }
      }
      break;
    // Handle the cases of pawn capture with promotion (ex: exd8Q) and ambiguous
    // non-pawn moves requiring both a specified origin rank and file
    // (ex: Qh4e1).
    case 5:
      if (piece_type == kPawn) {
        if (user_cmd[1] != 'x') {
          return false;
        }
        capture_indicated = true;
        char promoted_piece_ch = user_cmd[4];
        switch (promoted_piece_ch) {
          case 'N':
            promoted_piece = kKnight;
            break;
          case 'B':
            promoted_piece = kBishop;
            break;
          case 'R':
            promoted_piece = kRook;
            break;
          case 'Q':
            promoted_piece = kQueen;
            break;
          default:
            return false;
        }
        origin_file = user_cmd[0] - 'a';
        dest_file = user_cmd[2] - 'a';
        dest_rank = user_cmd[3] - '1';
      } else {
        origin_file = user_cmd[1] - 'a';
        origin_rank = user_cmd[2] - '1';
        dest_file = user_cmd[3] - 'a';
        dest_rank = user_cmd[4] - '1';
      }
      break;
    // Handle the case of an ambiguous non-pawn capture requiring specified
    // origin rank and file (ex: Qh4xe1)
    case 6:
      if (piece_type == kPawn || user_cmd[3] != 'x') {
        return false;
      }
      capture_indicated = true;
      origin_file = user_cmd[1] - 'a';
      origin_rank = user_cmd[2] - '1';
      dest_file = user_cmd[4] - 'a';
      dest_rank = user_cmd[5] - '1';
      break;
    // Handle the case of an en passant (ex: exd6e.p.)
    case 8:
      if (piece_type != kPawn || user_cmd[1] != 'x'
          || user_cmd.substr(4, 4) != "e.p.") {
        return false;
      }
      capture_indicated = true;
      is_ep = true;
      origin_file = user_cmd[0] - 'a';
      dest_file = user_cmd[2] - 'a';
      dest_rank = user_cmd[3] - '1';
      break;
    default:
      return false;
  }

  // Check for an invalid square position.
  if (origin_file != kNA && (origin_file < kA || origin_file > kH) ||
      origin_rank != kNA && (origin_rank < k1 || origin_rank > k8) ||
      dest_file < kA || dest_file > kH || dest_rank < k1 || dest_rank > k8) {
    return false;
  }

  int dest_square = kNumFiles * dest_rank + dest_file;
  // Check psuedo legality of the entered move.
  err_msg = "ambiguous or illegal piece movement specied";
  if (piece_type == kPawn) {
    // TODO: check if a pawn movement is psuedo legal.
    return false;
  } else {
    // Get all possible places the moved piece could move to from it's ending
    // position (origins) and remove all positions where a piece of this type
    // doesn't exist on the board before the move.
    Bitboard origins = board_.GetAttackMask(player, dest_square, piece_type);
    Bitboard moving_piece_positions = board_.GetPiecesByType(piece_type,
                                                             player);
    origins &= moving_piece_positions;
    // Check that only one bit is set in the origins mask
    bool one_origin_piece = origins && !(origins & (origins - 1));
    if (!one_origin_piece) {
      return false;
    }
  }

  entered_move.capture_indicated = capture_indicated;
  entered_move.is_ep = is_ep;
  entered_move.promoted_piece = promoted_piece;
  entered_move.castling_type = kNA;
  entered_move.dest = dest_square;

  return true;
}

void Game::CheckGameStatus() const {
  // TODO: Check if a player is now in check.
  // TODO: Check if the game has just ended in a stalemate and set
  // game_active_ equal to false. (Zobrist hashing happens here!)
  // TODO: Check if a player has been checkmated, set winner_ equal to the
  // player not in checkmate, and set game_active_ to false.
}

void Game::DisplayBoard() const {
  int piece, player;
  for (int rank = k8; rank >= k1; --rank) {
    cout << rank + 1 << " ";
    for (int file = kA; file <= kH; ++file) {
      piece = board_.GetPieceOnSquare(rank, file);
      player = board_.GetPlayerOnSquare(rank, file);
      string piece_symbol = piece_symbols_.at(make_pair(player, piece));
      cout << piece_symbol << " ";
    }
    cout << endl;
  }
  cout << "  A B C D E F G H" << endl;
}

bool Game::MakeMove(int player, Move move, string& err_msg) {
  // TODO: Check if the player that moved put their king in check.
  // TODO: Update the 8x8 representation.
  // TODO: Incrementally update the bitboards and attack maps.
  return true;
}
