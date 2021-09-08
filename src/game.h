/* Noah Himed
*
* Define the Game type. Game contains a board representation and functions to
* display, modify, and test the board.
*
* Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
*/

#ifndef OMEGAZERO_SRC_GAME_H_
#define OMEGAZERO_SRC_GAME_H_

#include <string>
#include <map>
#include <unordered_map>

#include "board.h"
#include "engine.h"
#include "move.h"

const int kHalfmoveClockLimit = 75;
const int kMaxPerftDepth = 14;

std::string GetPlayerStr(const int& player);

class Game {
public:
  Game(const std::string& init_pos);

  bool IsActive() const;

  void OutputWinner() const;
  // Debugging function to walk the move generation tree of strictly legal
  // moves to count all leaf nodes of a certain depth, which can be compared to
  // predetermined values and used to isolate bugs. Returns the number of moves
  // at the given depth.
  void Perft(const int& depth);
  void Play();
private:
  // Parse algebraic notation denoting a chess move, return if the move is
  // peudo-legal, and construct a corresponding Move struct.
  bool CheckMove(const std::string& user_cmd, Move& user_move,
                 std::string& err_msg);

  std::string GetPerftMoveStr(const Move& move) const;

  void CheckGameStatus();
  void DisplayBoard() const;

  Board board_;

  bool game_active_;

  Engine engine_;

  int player_to_move_;
  int player_in_check_;
  int winner_;

  // Use a map over an unordered map to allow the use of pairs as keys.
  std::string piece_symbols_[kNumPlayers][kNumPieceTypes];

  // Keep track of the number of times positions have occured during a game.
  std::unordered_map<U64, int> pos_rep_table_;
};

#endif // OMEGAZERO_SRC_GAME_H_
