/* Noah Himed
 *
 * ThreadSanitizer harness: runs multi-threaded (Lazy SMP) search over several
 * positions to validate the lockless transposition table under real concurrent
 * access.
 * Build with: make tsan
 * Run with:   ./build/tsan_harness [threads] [search_time]
 * A clean run (no ThreadSanitizer reports, exit 0) means the shared TT holds up
 * under concurrent probes and updates.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "board.h"
#include "move.h"
#include "search_pool.h"

using omegazero::Board;
using omegazero::Move;
using omegazero::S8;
using omegazero::SearchPool;
using omegazero::U64;
using std::cout;
using std::endl;
using std::string;
using std::vector;

auto main(int argc, char* argv[]) -> int {
  // A spread of positions (opening, tactical middlegames, an endgame) so the
  // worker threads generate heavy, varied concurrent TT traffic.
  const vector<string> kPositions = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  };

  S8 threads = 4;
  float search_time = 0.5f;
  if (argc > 1) threads = static_cast<S8>(std::atoi(argv[1]));
  if (argc > 2) search_time = static_cast<float>(std::atof(argv[2]));

  cout << "TSan Lazy-SMP harness: " << static_cast<int>(threads) << " threads, "
       << search_time << "s/position" << endl;

  for (const string& fen : kPositions) {
    Board board(fen);
    vector<U64> pos_history = {board.GetBoardHash()};
    SearchPool pool(threads);
    Move best = pool.LazySmpSearch(board, pos_history, search_time);
    cout << "  " << fen << " -> " << (best.IsEmpty() ? "NO MOVE" : "ok")
         << endl;
  }
  cout << "done" << endl;
  return EXIT_SUCCESS;
}
