// Noah Himed
// 18 March 2021
//
// Define the Game type. Game contains a board representation and functions to
// both display and modify the board.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#ifndef OMEGAZERO_SRC_GAME_H_
#define OMEGAZERO_SRC_GAME_H_

#include "board.h"
#include "constants.h"
#include "string.h"

#include <unordered_map>

class Game {
public:
  Game();
  void move(Player player);
  // Check if the game has ended in a win or draw
  bool isActive() const;
private:
  void displayBoard() const;

  Board board_;
  bool is_game_active_;
  std::unordered_map<int, std::string> piece_symbols_;
};

#endif // OMEGAZERO_SRC_GAME_H_
