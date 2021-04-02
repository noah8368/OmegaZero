// Noah Himed
// 18 March 2021
//
// Define the Game type. Game contains a board representation and functions to
// both display and modify the board.
//
// Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".

#ifndef OMEGAZERO_SRC_GAME_H_
#define OMEGAZERO_SRC_GAME_H_

#include <string>
#include <map>

#include "board.h"
#include "constants.h"
#include "move.h"

using namespace std;

class Game {
public:
  Game();
  void OutputGameResolution() const;
  void Play(int player);
  bool IsActive() const;
private:
  // Parse algebraic notation denoting a chess move, return if the move is
  // legal, and construct a corresponding Move struct.
  bool CheckMove(string user_cmd, Move& user_move, string& err_msg, int player);
  bool MakeMove(int player, Move move, string& err_msg);
  // Inspect the board representation to see if the conditions for a checkmate,
  // check, or stalemate have occured.
  void CheckGameStatus() const;
  // Loop through 8x8 board representation and print corresponding symbols.
  void DisplayBoard() const;

  Board board_;
  bool game_active_;
  int player_in_check_;
  int winner_;
  // Use a map over an unordered map to allow the use of pairs as keys.
  map<pair<int, int>, string> piece_symbols_;
};

#endif // OMEGAZERO_SRC_GAME_H_
