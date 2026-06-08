/* Noah Himed
 *
 * Perft harness — counts leaf nodes at a given depth to verify move generation.
 * Build with: make perft
 * Run with:   ./build/perft_harness <depth> [fen]
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "engine.h"
#include "move.h"

namespace omegazero {

using std::cout;
using std::endl;
using std::string;
using std::vector;

void RunPerft(const string& fen, int depth) {
  Board board(fen);
  Engine engine(&board, 'w', 0.5f);

  U64 total = engine.Perft(depth);

  vector<Move> moves = engine.GenerateMoves();
  for (Move& m : moves) {
    try {
      board.MakeMove(m);
    } catch (BadMove&) {
      continue;
    }
    U64 sub = (depth > 1) ? engine.Perft(depth - 1) : 1;
    board.UnmakeMove(m);

    S8 start_file = GetFileFromSq(m.start_sq);
    S8 start_rank = GetRankFromSq(m.start_sq);
    S8 target_file = GetFileFromSq(m.target_sq);
    S8 target_rank = GetRankFromSq(m.target_sq);

    string move_str;
    if (m.castling_type == kKingSide) {
      move_str = "0-0";
    } else if (m.castling_type == kQueenSide) {
      move_str = "0-0-0";
    } else {
      move_str += static_cast<char>('a' + start_file);
      move_str += static_cast<char>('1' + start_rank);
      move_str += static_cast<char>('a' + target_file);
      move_str += static_cast<char>('1' + target_rank);
      if (m.promoted_to_piece != kNA) {
        const char kPromoChars[] = {0, 'n', 'b', 'r', 'q'};
        move_str += kPromoChars[m.promoted_to_piece];
      }
    }
    cout << move_str << ": " << sub << endl;
  }

  cout << "\nTotal: " << total << endl;
}

}  // namespace omegazero

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    std::cout << "Usage: perft_harness <depth> [fen]" << std::endl;
    return 1;
  }

  int depth = std::atoi(argv[1]);
  std::string fen = (argc >= 3) ? argv[2]
      : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  omegazero::RunPerft(fen, depth);
  return 0;
}
