// Noah Himed
// 18 March 2021
//
// Implement the Game type.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "board.h"
#include "constants.h"
#include "game.h"
#include "move.h"

#include <iostream>
#include <string.h>

Game::Game() {
  game_active_ = true;

  piece_symbols_ = {{kEmpty, "."},
                    {kWhite * kPawn, "♙"}, {kWhite * kKnight, "♘"},
                    {kWhite * kBishop, "♗"}, {kWhite * kRook, "♖"},
                    {kWhite * kQueen, "♕"}, {kWhite * kKing, "♔"},
                    {kBlack * kPawn, "♟"}, {kBlack * kKnight, "♞"},
                    {kBlack * kBishop, "♝"}, {kBlack * kRook, "♜"},
                    {kBlack * kQueen, "♛"}, {kBlack * kKing, "♚"}};
  int player_in_check_ = kNA;
  int winner_ = kNA;
}

void Game::outputGameResolution() {
  if (winner_ == kNA) {
    std::cout << "Game has ended in a stalemate" << std::endl;
  } else {
    std::string winner_name = (winner_ == kWhite) ? "WHITE" : "BLACK";
    std::cout << winner_name << " wins" << std::endl;
  }
}

void Game::play(Player player) {
  displayBoard();

  std::string player_name = (player == kWhite) ? "WHITE" : "BLACK";
  std::cout << "\n\n" << player_name << " to move" << std::endl;

  std::string user_cmd;
  std::cout << "Enter move: ";
  std::getline(std::cin, user_cmd);
  Move user_move;
  std::string err_msg;
  bool move_legal = interpretCmd(user_cmd, user_move, err_msg);

  while (!move_legal) {
    std::cout << "ERROR: " << err_msg << std::endl
              << "Enter new move: ";
    std::getline(std::cin, user_cmd);
    move_legal = interpretCmd(user_cmd, user_move, err_msg);
  }
  std::cout << "\n\n";

  updateBoard(user_move);
  // Only check the game status if the player didn't just resign.
  if (game_active_) {
    checkGameStatus();
  }
}

bool Game::isActive() const {
  return game_active_;
}

bool Game::interpretCmd(std::string user_cmd, Move& move,
                        std::string& err_msg) {
  err_msg = "bad command formatting";
  size_t cmd_len = user_cmd.length();
  // Handle the case in which input is too short for any possible command.
  if (cmd_len == 0) {
    return false;
  }

  // Handle the case in which the user resigns.
  if (user_cmd == "r") {
    game_active_ = false;
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

  // Check for an invalid position.
  if (origin_file != kNA && (origin_file < kA || origin_file > kH) ||
      origin_rank != kNA && (origin_rank < k1 || origin_rank > k8) ||
      dest_file < kA || dest_file > kH || dest_rank < k1 || dest_rank > k8) {
    return false;
  }

  entered_move.capture_indicated = capture_indicated;
  entered_move.is_ep = is_ep;
  entered_move.promoted_piece = promoted_piece;
  entered_move.castling_type = kNA;
  entered_move.dest = static_cast<Square>(kNumFiles * dest_rank + dest_file);

  // TODO: Check psuedo legality of move. Use error "illegal piece movement"
  // TODO: Check if the move puts king in check. Use error
  // "move leaves king in check"

  return true;
}

void Game::checkGameStatus() const {
  // TODO: Check if a player is now in check.
  // TODO: Check if the game has just ended in a stalemate and set
  // game_active_ equal to false.
  // TODO: Check if a player has been checkmated, set winner_ equal to the
  // player not in checkmate, and set game_active_ to false.
}

void Game::displayBoard() const {
  int piece;
  // Loop through 8x8 board representation and print corresponding symbols.
  for (int rank = k8; rank >= k1; --rank) {
    std::cout << rank + 1 << " ";
    for (int file = kA; file <= kH; ++file) {
      piece = board_.getPieceOnSquare(rank, file);
      std::string piece_symbol = piece_symbols_.at(piece);
      std::cout << piece_symbol << " ";
    }
    std::cout << std::endl;
  }
  std::cout << "  A B C D E F G H" << std::endl;
}

void Game::updateBoard(Move move) {
  // TODO: Update the 8x8 representation.
  // TODO: Incrementally update the bitboards and attack maps.
}
