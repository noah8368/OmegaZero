/* Noah Himed
*
* Define the Game type. Game contains a board representation and functions to
* both display and modify the board.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

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
  bool IsActive() const;
  void OutputGameResolution() const;
  void Play(int player);
private:
  // Parse algebraic notation denoting a chess move, return if the move is
  // legal, and construct a corresponding Move struct.
  bool CheckMove(string user_cmd, Move& user_move, string& err_msg, int player);
  void CheckGameStatus() const;
  void DisplayBoard() const;

  Board board_;
  bool game_active_;
  int player_in_check_;
  int winner_;
  // Use a map over an unordered map to allow the use of pairs as keys.
  map<pair<int, int>, string> piece_symbols_;
};

#endif // OMEGAZERO_SRC_GAME_H_
