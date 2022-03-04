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

#include "board.h"
#include "engine.h"
#include "move.h"

namespace omegazero {

using std::cout;
using std::endl;
using std::string;
using std::unordered_map;

auto OneSqSet(const Bitboard& board) -> bool;

auto GetPieceLetter(S8 piece) -> char;

auto GetPlayerStr(S8 player) -> string;

auto GetPieceType(char piece_ch) -> S8;

class Game {
 public:
  Game(const string& init_pos, char player_side, float search_time);

  auto IsActive() const -> bool;

  auto OutputWinner() const -> void;
  auto Play() -> void;
  // Output the results of Perft in readable format.
  auto Test(int depth) -> void;

 private:
  // Construct a Move struct from a user command.
  auto ParseMoveCmd(const string& user_cmd) -> Move;

  // Construct a string denoting a move in FIDE standard algebraic notation.
  auto GetFideMoveStr(const Move& move) -> string;
  // Construct a string denoting a move in UCI standard algebraic notation.
  auto GetUciMoveStr(const Move& move) -> string;

  auto AddStartSqToMove(Move& move, S8 start_rank, S8 start_file,
                        S8 target_rank, S8 target_file,
                        bool capture_indicated) const -> void;
  auto CheckMove(Move& move, S8 start_rank, S8 start_file, S8 target_rank,
                 S8 target_file, bool capture_indicated) -> void;
  auto DisplayBoard() const -> void;
  auto InterpAlgNotation(const string& user_cmd, Move& move, S8& start_rank,
                         S8& start_file, S8& target_rank, S8& target_file,
                         bool& capture_indicated) -> void;
  auto RecordBoardState() -> void;

  Board board_;

  bool game_active_;

  Engine engine_;

  float search_time_;

  S8 winner_;

  string piece_symbols_[kNumPlayers][kNumPieceTypes];

  // Implement a custom hash function for unordered_map that calls the board's
  // hash function.
  struct BoardKeyHash {
    size_t operator()(const Board& board) const {
      return static_cast<size_t>(board.GetBoardHash());
    }
  };
  unordered_map<Board, S8, BoardKeyHash> pos_history_;
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
    cout << "\nDraw" << endl;
  } else {
    string player_name = GetPlayerStr(winner_);
    cout << "\n" << player_name << " wins" << endl;
  }
}

inline auto Game::RecordBoardState() -> void {
  if (pos_history_.find(board_) == pos_history_.end()) {
    pos_history_[board_] = 1;
  } else {
    ++pos_history_[board_];
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_GAME_H_
