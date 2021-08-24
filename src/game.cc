/* Noah Himed
*
* Implement the Game type.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/


#include "game.h"

#include <math.h>
#include <string.h>

#include <iostream>

#include "board.h"
#include "constants.h"
#include "move.h"

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
  int ep_target_sq_ = kNA;
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
    // Check if the player has resigned.
    if (user_cmd == "r") {
      game_active_ = false;
      break;
    } else {
      // Check if the move is pseudo-legal.
      move_legal = CheckMove(user_cmd, user_move, err_msg, player);
      if (move_legal) {
        // Make the move and check if it puts the player's king in check.
        // If so, unmake the move and set err_msg accordingly.
        move_legal = board_.MakeMove(user_move, err_msg);
      }
    }
    // Output an appropriate error message for illegal moves.
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
  size_t cmd_len = user_cmd.length();
  move.moving_player = player;
  if (cmd_len == 0) {
    err_msg = "bad command formatting";
    return false;
  } else if (user_cmd == "0-0") {
    move.castling_type = kKingSide;
    return true;
  } else if (user_cmd == "0-0-0") {
    move.castling_type = kQueenSide;
    return true;
  }

  char first_ch = user_cmd[0];
  switch (first_ch) {
    case 'N':
      move.moving_piece = kKnight;
      break;
    case 'B':
      move.moving_piece = kBishop;
      break;
    case 'R':
      move.moving_piece = kRook;
      break;
    case 'Q':
      move.moving_piece = kQueen;
      break;
    case 'K':
      move.moving_piece = kKing;
      break;
    default:
      move.moving_piece = kPawn;
      int file = first_ch - 'a';
      if (file < kA || file > kH) {
        err_msg = "bad command formatting";
        return false;
      }
  }

  bool capture_indicated = false;
  int end_rank, end_file;
  int start_rank = kNA;
  int start_file = kNA;
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
      if (move.moving_piece != kPawn) {
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
            move.promoted_piece = kKnight;
            break;
          case 'B':
            move.promoted_piece = kBishop;
            break;
          case 'R':
            move.promoted_piece = kRook;
            break;
          case 'Q':
            move.promoted_piece = kQueen;
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
      if (move.moving_piece == kPawn) {
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
      if (move.moving_piece == kPawn) {
        if (user_cmd[1] != 'x') {
          err_msg = "bad command formatting";
          return false;
        }
        capture_indicated = true;
        char promoted_piece_ch = user_cmd[4];
        switch (promoted_piece_ch) {
          case 'N':
            move.promoted_piece = kKnight;
            break;
          case 'B':
            move.promoted_piece = kBishop;
            break;
          case 'R':
            move.promoted_piece = kRook;
            break;
          case 'Q':
            move.promoted_piece = kQueen;
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
      if (move.moving_piece == kPawn || user_cmd[3] != 'x') {
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
      if (move.moving_piece != kPawn || user_cmd[1] != 'x'
          || user_cmd.substr(4, 4) != "e.p.") {
        err_msg = "bad command formatting";
        return false;
      } else {
        capture_indicated = true;
        move.is_ep = true;
        start_file = user_cmd[0] - 'a';
        end_file = user_cmd[2] - 'a';
        end_rank = user_cmd[3] - '1';
      }
      break;
    default:
      err_msg = "bad command formatting";
      return false;
  }

  // Check that specified square positions are on the board.
  if (start_file != kNA && (start_file < kA || start_file > kH) ||
      start_rank != kNA && (start_rank < k1 || start_rank > k8) ||
      end_file < kA || end_file > kH || end_rank < k1 || end_rank > k8) {
    err_msg = "bad command formatting";
    return false;
  }

  // Confirm a capturing move lands on a square occupied by the other player.
  int other_player = player ^ 1;
  if (capture_indicated && !move.is_ep) {
    if (board_.GetPlayerOnSquare(end_rank, end_file) != other_player) {
      err_msg = "invalid capture indicated";
      return false;
    } else {
      move.captured_piece = board_.GetPieceOnSquare(end_rank, end_file);
    }
  }
  move.end_sq = kNumFiles * end_rank + end_file;

  // Compute start_sq by getting all possible places the moved piece could move
  // to from its ending position (start_positions) and remove all positions
  // where a piece of this type doesn't exist on the board before the move.
  Bitboard start_positions;
  int start_sq = kNA;
  if (move.moving_piece == kPawn) {
    // Handle en passent moves. Note that we needn't check if all the
    // conditions for an en passent have been met here because ep_target_sq_
    // will only be initialized to a valid square in this scenario.
    if (move.is_ep) {
      // Handle the case of White making an en passent.
      int ep_target_sq = board_.GetEpTargetSquare();
      if (move.end_sq == ep_target_sq
          && abs(start_file - end_file) == 1
          && ((player == kWhite 
              && board_.GetPieceOnSquare(k5, start_file) == kPawn
              && board_.GetPlayerOnSquare(k5, start_file) == kWhite))) {
        move.start_sq = kNumFiles * k5 + start_file;
        move.captured_piece = kPawn;
        return true;
      // Handle the case of a black player making en passent.
      } else if (move.end_sq == ep_target_sq
                 && abs(start_file - end_file) == 1
                 && ((player == kBlack
                     && board_.GetPieceOnSquare(k4, start_file) == kPawn
                     && board_.GetPlayerOnSquare(k4, start_file) == kBlack))) {
        move.start_sq = kNumFiles * k4 + start_file;
        move.captured_piece = kPawn;
        return true;
      } else {
        err_msg = "illegal en passent specified";
        return false;
      }
    // Handle the case of White making a double pawn push.
    } else if (player == kWhite && end_rank == k4 && !capture_indicated
               && board_.GetPieceOnSquare(k3, end_file) == kNA
               && board_.GetPieceOnSquare(k2, end_file) == kPawn
               && board_.GetPlayerOnSquare(k2, end_file) == kWhite) {
      move.start_sq = kNumFiles * k2 + end_file;
      move.new_ep_target_sq = kNumFiles * k3 + end_file;
      return true;
    // Handle the case of Black making a double pawn push.
    } else if (player == kBlack && end_rank == k5 && !capture_indicated
               && (board_.GetPieceOnSquare(k6, end_file) == kNA)
               && (board_.GetPieceOnSquare(k7, end_file) == kPawn)
               && (board_.GetPlayerOnSquare(k7, end_file) == kBlack)) {
      move.start_sq = kNumFiles * k7 + end_file;
      move.new_ep_target_sq = kNumFiles * k6 + end_file;
      return true;
    } else {
      start_positions = board_.GetAttackMask(other_player, move.end_sq, kPawn);
    }
    // Clear off pieces on or off the same file as the ending position
    // depending on if the move is a capture or not.
    if (capture_indicated) {
      start_positions &= ~kFileMasks[end_file];
    } else {
      start_positions &= kFileMasks[end_file];
    }
  } else {
    start_positions = board_.GetAttackMask(player, move.end_sq,
                                           move.moving_piece);
  }
  start_positions &= board_.GetPiecesByType(move.moving_piece, player);
  if (start_file != kNA) {
    start_positions &= kFileMasks[start_file];
  }
  if (start_rank != kNA) {
    start_positions &= kRankMasks[start_rank];
  }

  // Check that exactly one bit is set in the start_positions mask.
  bool one_origin_piece = start_positions
                          && !(start_positions & (start_positions - 1));
  if (one_origin_piece) {
    move.start_sq = log2(start_positions);
    // ep_target_sq_ = kNA;
    return true;
  } else {
    err_msg = "ambiguous or illegal piece movement specified";
    return false;
  }
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
