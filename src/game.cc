// Noah Himed
// 18 March 2021
//
// Implement the Game type.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "game.h"

#include <math.h>
#include <string.h>

#include <iostream>

#include "board.h"
#include "constants.h"
#include "move.h"

// debug library
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
    move_legal = CheckMove(user_cmd, &user_move, err_msg, player);
    if (move_legal) {
      // Check if the player has resigned.
      if (game_active_) {
        // Check if the move puts the player's king in check.
        move_legal = board_.MakeMove(user_move, err_msg);
      } else {
        break;
      }
    } else {
      cout << "ERROR: " << err_msg << endl;
    }
  } while (!move_legal);
  cout << "\n\n";

  CheckGameStatus();
}

bool Game::IsActive() const {
  return game_active_;
}

bool Game::CheckMove(string user_cmd, Move* move,
                     string& err_msg, int player) {
  size_t cmd_len = user_cmd.length();
  if (cmd_len == 0) {
    err_msg = "bad command formatting";
    return false;
  }

  // Handle the case in which the user resigns.
  if (user_cmd == "r") {
    game_active_ = false;
    winner_ = player ^ 1;
    return true;
  }

  if (user_cmd == "0-0") {
    move->castling_type = kKingSide;
    return true;
  }
  if (user_cmd == "0-0-0") {
    move->castling_type = kQueenSide;
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
        err_msg = "bad command formatting";
        return false;
      }
  }

  bool capture_indicated = false;
  bool is_ep = false;
  int end_rank, end_file;
  int start_rank = kNA;
  int start_file = kNA;
  int promoted_piece = kNA;
  switch (cmd_len) {
    // Handle the case of unambiguous pawn move without capture (ex: e4).
    case 2:
      end_file = user_cmd[0] - 'a';
      end_rank = user_cmd[1] - '1';
      // Declare invalid if a pawn move to rank 8 is indicated without
      // choosing a piece type to promote to.
      if (end_rank == k8) {
        err_msg = "bad command formatting";
        return false;
      }
      break;
    // Handle the cases of unambiguous non-pawn moves without capture (ex: Qe4)
    // and unambiguous pawn move and promotion (ex: d8Q).
    case 3:
      if (piece_type != kPawn) {
        end_file = user_cmd[1] - 'a';
        end_rank = user_cmd[2] - '1';
      } else {
        end_rank = user_cmd[1] - '1';
        if (end_rank != k8) {
          err_msg = "bad command formatting";
          return false;
        }
        end_file = user_cmd[0] - 'a';
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
            err_msg = "bad command formatting";
            return false;
        }
      }
      break;
    // Handle the cases of unambiguous captures (ex: exd6, Nxe4) and
    // ambiguous moves requiring a specified origin rank or file
    // (ex: R1a3, Rdf8).
    case 4:
      end_file = user_cmd[2] - 'a';
      end_rank = user_cmd[3] - '1';
      if (piece_type == kPawn) {
        if (user_cmd[1] != 'x') {
          err_msg = "bad command formatting";
          return false;
        }
        capture_indicated = true;
        start_file = user_cmd[0] - 'a';
      } else {
        char second_ch = user_cmd[1];
        if (second_ch - '1' >= k1 && second_ch - '1' <= k8) {
          start_rank = second_ch - '1';
        } else if (second_ch - 'a' >= kA && second_ch - 'a' <= kH) {
          start_file = second_ch - 'a';
        } else if (second_ch == 'x') {
          capture_indicated = true;
        } else {
          err_msg = "bad command formatting";
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
          err_msg = "bad command formatting";
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
            err_msg = "bad command formatting";
            return false;
        }
        start_file = user_cmd[0] - 'a';
        end_file = user_cmd[2] - 'a';
        end_rank = user_cmd[3] - '1';
      } else {
        start_file = user_cmd[1] - 'a';
        start_rank = user_cmd[2] - '1';
        end_file = user_cmd[3] - 'a';
        end_rank = user_cmd[4] - '1';
      }
      break;
    // Handle the case of an ambiguous non-pawn capture requiring specified
    // origin rank and file (ex: Qh4xe1)
    case 6:
      if (piece_type == kPawn || user_cmd[3] != 'x') {
        err_msg = "bad command formatting";
        return false;
      }
      capture_indicated = true;
      start_file = user_cmd[1] - 'a';
      start_rank = user_cmd[2] - '1';
      end_file = user_cmd[4] - 'a';
      end_rank = user_cmd[5] - '1';
      break;
    // Handle the case of an en passant (ex: exd6e.p.)
    case 8:
      if (piece_type != kPawn || user_cmd[1] != 'x'
          || user_cmd.substr(4, 4) != "e.p.") {
        err_msg = "bad command formatting";
        return false;
      }
      capture_indicated = true;
      is_ep = true;
      start_file = user_cmd[0] - 'a';
      end_file = user_cmd[2] - 'a';
      end_rank = user_cmd[3] - '1';
      break;
    default:
      err_msg = "bad command formatting";
      return false;
  }

  // Check for invalid specified square positions.
  if (start_file != kNA && (start_file < kA || start_file > kH) ||
      start_rank != kNA && (start_rank < k1 || start_rank > k8) ||
      end_file < kA || end_file > kH || end_rank < k1 || end_rank > k8) {
    err_msg = "bad command formatting";
    return false;
  }

  int other_player = player ^ 1;
  int captured_piece = kNA;
  if (capture_indicated) {
    if (board_.GetPlayerOnSquare(end_rank, end_file) != other_player) {
      err_msg = "invalid capture indicated";
      return false;
    } else {
      captured_piece = board_.GetPieceOnSquare(end_rank, end_file);
    }
  }

  // Get all possible places the moved piece could move to from its ending
  // position (start_positions) and remove all positions where a piece of this
  // type doesn't exist on the board before the move.
  Bitboard start_positions;
  int end_sq = kNumFiles * end_rank + end_file;
  if (piece_type == kPawn) {
    // Handle the case of double pawn pushes.

    // PROBLEM: The case for dxe4 is unhandled if no piece occupies e3.
    if (player == kWhite && end_rank == k4
        && (board_.GetPieceOnSquare(k3, end_file) == kNA)) {
      int double_push_sq = kNumFiles * k7 + end_file;
      start_positions = board_.GetAttackMask(kBlack, double_push_sq, kPawn);
      // Shift the attack mask back down to the 4th rank.
      start_positions >>= (3*kNumFiles);
    } else if (player == kBlack && end_rank == k5
               && (board_.GetPieceOnSquare(k6, end_file) == kNA)) {
      int double_push_sq = kNumFiles * k2 + end_file;
      start_positions = board_.GetAttackMask(kWhite, double_push_sq, kPawn);
      // Shift the attack mask back down to the 4th rank.
      start_positions <<= (3*kNumFiles);
    } else {
      start_positions = board_.GetAttackMask(other_player, end_sq, kPawn);
    }

    if (!capture_indicated) {
      start_positions &= kFileMasks[end_file];
    }
  } else {
    start_positions = board_.GetAttackMask(player, end_sq, piece_type);
  }
  Bitboard matching_piece_positions = board_.GetPiecesByType(piece_type,
                                                             player);
  start_positions &= matching_piece_positions;
  if (start_file != kNA) {
    start_positions &= kFileMasks[start_file];
  }
  if (start_rank != kNA) {
    start_positions &= kRankMasks[start_rank];
  }

  // Check that exactly one bit is set in the start_positions mask.
  bool one_origin_piece = start_positions
                          && !(start_positions & (start_positions - 1));
  if (!one_origin_piece) {
    err_msg = "ambiguous or illegal piece movement specied";
    return false;
  }
  int start_sq = log2(start_positions);

  move->is_ep = is_ep;
  move->captured_piece = captured_piece;
  move->castling_type = kNA;
  move->end_sq = end_sq;
  move->moving_piece = piece_type;
  move->moving_player = player;
  move->start_sq = start_sq;
  move->promoted_piece = promoted_piece;

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
