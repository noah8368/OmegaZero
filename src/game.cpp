// Noah Himed
// 18 March 2021
//
// Implement the Game type.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#include "board.h"
#include "constants.h"
#include "game.h"

#include <iostream>
#include <string.h>

Game::Game() {
  is_game_active_ = true;

  piece_symbols_ = {{kEmpty, "."},
                    {kWhite * kPawn, "♙"}, {kWhite * kKnight, "♘"},
                    {kWhite * kBishop, "♗"}, {kWhite * kRook, "♖"},
                    {kWhite * kQueen, "♕"}, {kWhite * kKing, "♔"},
                    {kBlack * kPawn, "♟"}, {kBlack * kKnight, "♞"},
                    {kBlack * kBishop, "♝"}, {kBlack * kRook, "♜"},
                    {kBlack * kQueen, "♛"}, {kBlack * kKing, "♚"}};
}

void Game::move(Player player) {
  displayBoard();

  std::string player_name = (player == kWhite) ? "WHITE" : "BLACK";
  std::cout << "\n\n" << player_name << " to move" << std::endl;

  std::string move;
  std::cout << "Enter move: ";
  std::getline(std::cin, move);
  std::cout << "\n\n";

  // Exit program
  if (move == "x") {
    is_game_active_ = false;
  }
}

bool Game::isActive() const {
  return is_game_active_;
}

void Game::displayBoard() const {
  int piece;
  // Loop through 8x8 board representation and print corresponding symbols
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
