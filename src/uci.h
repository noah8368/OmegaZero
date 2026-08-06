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
  UciHandler(const std::string& book_path, const std::string& params_path);
  auto Run() -> void;

 private:
  auto HandleUci() -> void;
  auto HandleIsReady() -> void;
  auto HandleUciNewGame() -> void;
  auto HandlePosition(const std::string& line) -> void;
  auto HandleGo(const std::string& line) -> void;
  // UCI `ponderhit`: hand the running ponder search its real time budget.
  auto HandlePonderHit() -> void;
  // UCI `setoption name <N> value <V>`: update the runtime search parameter <N>.
  auto HandleSetOption(const std::string& line) -> void;
  // Body of the search worker thread: runs the search and prints `bestmove`.
  auto RunSearch() -> void;
  // Stop any in-progress search worker and join it (no-op if none running).
  auto StopSearch() -> void;

  auto MoveToUciStr(const Move& move) const -> std::string;
  // Side-aware variant: renders castling from `player`'s perspective rather than
  // the current side to move. Needed for PV moves, whose colors alternate.
  auto MoveToUciStr(const Move& move, S8 player) const -> std::string;
  // Format and print one `info` line for a completed search iteration. Wired to
  // the engine via SetInfoCallback; serialized with other output by cout_mutex_.
  auto PrintInfo(const SearchInfo& info) -> void;
  auto MoveToFideStr(const Move& move) const -> std::string;
  auto ParseUciMove(const std::string& uci_move) const -> Move;
  auto SetPosition(const std::string& fen,
                   const std::vector<std::string>& moves) -> void;
  auto LoadOpeningBook(const std::string& path) -> void;
  auto GetBookMove(Move& book_move) -> bool;

  static constexpr const char* kStartFen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  // Owned TT, injected into engine_ and reused across engine_ re-creation
  // (HandleUciNewGame clears it explicitly). Declared before engine_ so it
  // outlives the Engine that points at it.
  TranspositionTable tt_;
  std::unique_ptr<Board> board_;
  std::unique_ptr<Engine> engine_;
  // Current values of the tunable search parameters (UCI spin options). Source of
  // truth across engine_ re-creation; pushed into engine_ before each search.
  SearchParams uci_params_;
  std::vector<std::vector<std::string>> opening_book_;
  std::string book_path_;
  int turn_num_;
  int move_index_;
  bool on_opening_;

  // Ponder state: `pondering_` is true while a `go ponder` search runs (before
  // `ponderhit`/`stop`); ponder_soft_/ponder_hard_ hold the per-move budget (in
  // seconds) to impose on the search when `ponderhit` arrives.
  bool pondering_;
  float ponder_soft_;
  float ponder_hard_;

  // Search runs on this worker so the main loop can keep reading stdin (needed
  // to honor `stop` during `go infinite`). `cout_mutex_` serializes output so a
  // `readyok` from the main thread can't interleave with the worker's
  // `bestmove`.
  std::thread search_thread_;
  std::mutex cout_mutex_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_UCI_H_
