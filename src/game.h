/* Noah Himed
*
* Define the Game type. Game contains a board representation and functions to
* display, modify, and gather information about the board.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#ifndef OMEGAZERO_SRC_GAME_H_
#define OMEGAZERO_SRC_GAME_H_

#include <string>
#include <map>
#include <unordered_map>

#include "board.h"
#include "constants.h"
#include "move.h"

using namespace std;

class Game {
public:
  Game();

  bool IsActive() const;

  void OutputGameResolution() const;
  void Play();
private:
  // Parse algebraic notation denoting a chess move, return if the move is
  // peudo-legal, and construct a corresponding Move struct.
  bool CheckMove(string user_cmd, Move& user_move, string& err_msg,
                 const int& player);

  void CheckGameStatus();
  void DisplayBoard() const;

  Board board_;

  bool game_active_;

  int player_;
  int player_in_check_;
  int winner_;

  // Use a map over an unordered map to allow the use of pairs as keys.
  map<pair<int, int>, string> piece_symbols_;

  // Keep track of the number of times positions have occured during a game.
  unordered_map<uint64_t, int> pos_rep_table_;
};

#endif // OMEGAZERO_SRC_GAME_H_
