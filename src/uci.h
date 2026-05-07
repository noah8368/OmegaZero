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
#include <string>
#include <vector>

#include "board.h"
#include "engine.h"
#include "move.h"

namespace omegazero {

class UciHandler {
 public:
  UciHandler();
  auto Run() -> void;

 private:
  auto HandleUci() -> void;
  auto HandleIsReady() -> void;
  auto HandleUciNewGame() -> void;
  auto HandlePosition(const std::string& line) -> void;
  auto HandleGo(const std::string& line) -> void;

  auto MoveToUciStr(const Move& move) const -> std::string;
  auto ParseUciMove(const std::string& uci_move) const -> Move;
  auto ComputeThinkTime(int wtime, int btime, int winc, int binc,
                        int movetime, int movestogo) const -> float;
  auto SetPosition(const std::string& fen,
                   const std::vector<std::string>& moves) -> void;

  static constexpr const char* kStartFen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  std::unique_ptr<Board> board_;
  std::unique_ptr<Engine> engine_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_UCI_H_
