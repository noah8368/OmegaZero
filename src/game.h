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
#include <string>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "search_pool.h"

namespace omegazero {

using std::cout;
using std::endl;
using std::string;
using std::to_string;
using std::unordered_map;

auto GetPieceLetter(S8 piece) -> char;

auto GetPlayerStr(S8 player) -> string;

auto GetPieceType(char piece_ch) -> S8;

class Game {
 public:
  Game(const string& init_pos, const string& opening_book_path,
       char player_side, float search_time, bool on_opening = true,
       bool light_theme = false, int num_threads = DefaultThreadCount());

  auto SetClock(float base_time, float increment) -> void;

  // Override the engine's tunable search parameters (e.g. a params.json profile
  // resolved by the caller). Forwards to the owned Engine.
  auto SetSearchParams(const SearchParams& params) -> void {
    engine_.SetParams(params);
  }

  auto IsActive() const -> bool;
  auto GetOpeningMove(Move& opening_move) -> bool;

  auto MakeEngineMove() -> Move;

  auto GetWinner() const -> S8;

  auto OutputWinner() const -> void;
  auto Play() -> void;
  auto SavePgn(const string& opponent_name) -> void;

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
  auto RecordFinalScore() -> void;
  // NOTE: This should be called AFTER a move is made.
  auto UpdateMoveHistory(string move_str) -> void;
  auto DisplayClock() const -> void;

  Board board_;

  bool game_active_;
  bool on_opening_;
  bool clock_mode_;

  // Lazy SMP pool; owns the shared TT (injected into engine_ via GetTt()) and
  // manages helper threads. Declared before engine_ so it outlives the Engine
  // that points at its table. 1 thread means main only (no helpers).
  SearchPool pool_{1};
  Engine engine_;

  float search_time_;
  float increment_;
  float clock_[kNumPlayers];

  int turn_num_;
  // Each opening is a sequence of FIDE move strings.
  vector<vector<string>> opening_book_;
  vector<string> played_fide_moves_;

  S8 winner_;

  string move_history_;
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

inline auto Game::GetWinner() const -> S8 { return winner_; }

inline auto Game::OutputWinner() const -> void {
  if (winner_ == kNA) {
    cout << "\nDraw" << endl;
  } else {
    string player_name = GetPlayerStr(winner_);
    cout << "\n" << player_name << " wins" << endl;
  }
}

inline auto Game::RecordBoardState() -> void { ++pos_history_[board_]; }

inline auto Game::RecordFinalScore() -> void {
  if (winner_ == kWhite) {
    move_history_ += "1-0";
  } else if (winner_ == kBlack) {
    move_history_ += "0-1";
  } else {
    move_history_ += "1/2-1/2";
  }
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_GAME_H_
