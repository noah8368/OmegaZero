/* Noah Himed
 *
 * NPS benchmark harness. Searches standard positions for a fixed duration
 * and reports nodes/second.
 * Build with: make bench
 * Run with:   ./build/bench_harness [search_time]
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "board.h"
#include "engine.h"
#include "nnue.h"

namespace omegazero {

using std::cout;
using std::endl;
using std::ofstream;
using std::string;

struct SuppressCout {
  ofstream dev_null_;
  std::streambuf* saved_;
  SuppressCout() : dev_null_("/dev/null"), saved_(cout.rdbuf(dev_null_.rdbuf())) {}
  ~SuppressCout() { cout.rdbuf(saved_); }
};

struct BenchPos {
  const char* name;
  const char* fen;
};

const BenchPos kBenchPositions[] = {
  {"opening",  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
  {"midgame",  "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"},
  {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
  {"endgame",  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
};

void RunNpsBench(float search_time) {
  uint64_t total_nodes = 0;
  double total_elapsed = 0;

  cout << "position,nodes,elapsed_s,nps" << endl;

  for (const auto& pos : kBenchPositions) {
    Board board(pos.fen);
    TranspositionTable tt;
    Engine engine(&tt, &board, 'w', search_time);
    engine.AddPosToHistory();

    auto start = std::chrono::high_resolution_clock::now();
    {
      SuppressCout suppress;
      engine.GetBestMove();
    }
    auto end = std::chrono::high_resolution_clock::now();

    double elapsed = std::chrono::duration<double>(end - start).count();
    uint64_t nodes = engine.GetTotalNodes();
    uint64_t nps = elapsed > 0 ? static_cast<uint64_t>(nodes / elapsed) : 0;

    cout << pos.name << "," << nodes << ","
         << std::fixed << std::setprecision(4) << elapsed << ","
         << nps << endl;

    total_nodes += nodes;
    total_elapsed += elapsed;
  }

  uint64_t avg_nps = total_elapsed > 0
      ? static_cast<uint64_t>(total_nodes / total_elapsed) : 0;
  cout << "average,0,0," << avg_nps << endl;
}

}  // namespace omegazero

auto main(int argc, char* argv[]) -> int {
  using namespace omegazero;

  string exe_dir(argv[0]);
  size_t last_slash = exe_dir.rfind('/');
  exe_dir = (last_slash != string::npos) ? exe_dir.substr(0, last_slash + 1)
                                         : "./";

  string nnue_path = exe_dir + "../nnue/nnue.bin";
  if (g_nnue.Load(nnue_path)) {
    std::cerr << "NNUE: loaded " << nnue_path << std::endl;
  } else {
    std::cerr << "NNUE: not found, using HCE" << std::endl;
  }

  float search_time = 5.0f;
  if (argc > 1) search_time = std::atof(argv[1]);

  RunNpsBench(search_time);
  return 0;
}
