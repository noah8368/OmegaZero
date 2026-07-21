/* Noah Himed
 *
 * Define the UciHandler type. Implements the Universal Chess Interface (UCI)
 * protocol for communication with chess GUIs and tournament managers.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_UCI_H_
#define OMEGAZERO_SRC_UCI_H_

#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "board.h"
#include "engine.h"
#include "move.h"

namespace omegazero {

class UciHandler {
 public:
  explicit UciHandler(const std::string& book_path);
  auto Run() -> void;

 private:
  auto HandleUci() -> void;
  auto HandleIsReady() -> void;
  auto HandleUciNewGame() -> void;
  auto HandlePosition(const std::string& line) -> void;
  auto HandleGo(const std::string& line) -> void;
  // Body of the search worker thread: runs the search and prints `bestmove`.
  auto RunSearch() -> void;
  // Stop any in-progress search worker and join it (no-op if none running).
  auto StopSearch() -> void;

  auto MoveToUciStr(const Move& move) const -> std::string;
  auto MoveToFideStr(const Move& move) const -> std::string;
  auto ParseUciMove(const std::string& uci_move) const -> Move;
  auto SetPosition(const std::string& fen,
                   const std::vector<std::string>& moves) -> void;
  auto LoadOpeningBook(const std::string& path) -> void;
  auto GetBookMove(Move& book_move) -> bool;

  static constexpr const char* kStartFen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  std::unique_ptr<Board> board_;
  std::unique_ptr<Engine> engine_;
  std::vector<std::vector<std::string>> opening_book_;
  std::string book_path_;
  int turn_num_;
  int move_index_;
  bool on_opening_;

  // Search runs on this worker so the main loop can keep reading stdin (needed
  // to honor `stop` during `go infinite`). `cout_mutex_` serializes output so a
  // `readyok` from the main thread can't interleave with the worker's
  // `bestmove`.
  std::thread search_thread_;
  std::mutex cout_mutex_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_UCI_H_
