/* Noah Himed
 *
 * Define the Game type. Game contains a board representation and functions to
 * display, modify, and test the board.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_GAME_H_
#define OMEGAZERO_SRC_GAME_H_

#include <iostream>
#include <map>
#include <string>
#include <unordered_map>

#include "board.h"
#include "engine.h"
#include "move.h"

namespace omegazero {

using std::cout;
using std::endl;
using std::string;
using std::unordered_map;

constexpr S8 kHalfmoveClockLimit = 75;
constexpr S8 kMaxPerftDepth = 14;

auto OneSqSet(const Bitboard& board) -> bool;

auto GetPlayerStr(S8 player) -> string;

auto GetPieceType(char piece_ch) -> S8;

class Game {
 public:
  Game(const string& init_pos);

  auto IsActive() const -> bool;

  auto OutputWinner() const -> void;
  auto Play() -> void;
  // Output the results of Perft in readable format.
  auto Test(int depth) -> void;
  // Output speed statistics from searching for the best move.
  auto TimeSearch(int depth) -> void;

 private:
  // Construct a Move struct from a user command.
  auto ParseMoveCmd(const string& user_cmd) -> Move;

  auto GetUCIMoveStr(const Move& move) -> string;

  auto AddStartSqToMove(Move& move, S8 start_rank, S8 start_file,
                        S8 target_rank, S8 target_file,
                        bool capture_indicated) const -> void;
  auto CheckGameStatus() -> void;
  auto DisplayBoard() const -> void;
  auto CheckMove(Move& move, S8 start_rank, S8 start_file, S8 target_rank,
                 S8 target_file, bool capture_indicated) -> void;
  auto InterpAlgNotation(const string& user_cmd, Move& move, S8& start_rank,
                         S8& start_file, S8& target_rank, S8& target_file,
                         bool& capture_indicated) -> void;

  Board board_;

  bool game_active_;

  Engine engine_;

  S8 winner_;

  string piece_symbols_[kNumPlayers][kNumPieceTypes];

  // Keep track of the number of times positions have occured during a game.
  unordered_map<U64, S8> pos_rep_table_;
};

// Implement inline non-member functions.

inline auto OneSqSet(const Bitboard& board) -> bool {
  return board && !static_cast<bool>(board & (board - 1));
}

inline auto GetPlayerStr(S8 player) -> string {
  if (player == kWhite) {
    return "White";
  }
  if (player == kBlack) {
    return "Black";
  }

  throw invalid_argument("player in GetPlayerStr()");
}

// Implement inline member functions.

inline auto Game::IsActive() const -> bool { return game_active_; }

inline auto Game::OutputWinner() const -> void {
  if (winner_ == kNA) {
    cout << "Draw" << endl;
  } else {
    string player_name = GetPlayerStr(winner_);
    cout << player_name << " wins" << endl;
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_GAME_H_
