/* Noah Himed
 *
 * Uncertainty-head harness (unc-007). Reads FENs from stdin, one per line, and
 * for each prints the fused net's eval v and the predicted eval-error
 * distribution p(u | x) as a single JSON line: v, E[u|x], the Student-t mixture
 * params, and quantiles. This is the ONLY place the uncertainty head is called
 * from a binary; the engine (main) stays free of head code.
 *
 * With --vstar, it ALSO runs the same deterministic fixed-depth + node-capped
 * search datagen uses (cleared TT, from scratch) to get v_star, and reports the
 * realized error u = v - v_star, so the analysis can check calibration/coverage
 * against ground truth on curated positions (unc-007 P3).
 *
 * Build with: make unc_harness
 * Run with:   build/unc_harness [net_path] [--vstar [--depth N] [--nodes M]] < fens.txt
 *             (default net: the fused unc_research/models/nnue_unc.bin)
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "board.h"
#include "engine.h"
#include "nnue.h"
#include "params.h"
#include "transposition_table.h"

namespace omegazero {

using std::cerr;
using std::cin;
using std::endl;
using std::getline;
using std::string;

// Quantile levels emitted per position (one-sided margin reads live in the tail).
static const double kTaus[] = {0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99};

// v_star scores at/beyond this magnitude are forced-result sentinels (mate/TB),
// not smooth evals -- flagged so the analysis can exclude them (matches datagen).
static const int kDecisiveScoreThreshold = 20000;

// The deep target search, matching datagen's PlayGameUncertainty exactly: a
// clean from-scratch fixed-depth + node-capped search on a fresh TT. Returns
// v_star (STM POV); fills reached_depth and nodes.
static auto SearchVStar(Board& board, const SearchParams& params, int depth,
                        uint64_t node_cap, int& reached_depth, uint64_t& nodes)
    -> int {
  TranspositionTable tt;
  auto engine = std::make_unique<Engine>(&tt, &board, 'w', 5.0F);
  engine->SetParams(params);
  engine->AddPosToHistory();
  reached_depth = 0;
  engine->SetInfoCallback(
      [&reached_depth](const SearchInfo& info) { reached_depth = info.depth; });
  engine->SetInfiniteSearch();
  engine->SetDepthLimit(depth);
  engine->SetNodeLimit(node_cap);
  int v_star = 0;
  engine->GetBestMove(v_star);
  nodes = engine->GetTotalNodes();
  return v_star;
}

static auto PrintDistJson(const string& fen, const UncDist& d, int e_u_cp_i8,
                          bool with_vstar, int v_star, int u, int reached_depth,
                          uint64_t nodes) -> void {
  std::printf(
      "{\"fen\":\"%s\",\"v\":%d,\"e_u_cp\":%.6f,\"e_u_cp_i8\":%d,"
      "\"u_mean\":%.6f,\"u_std\":%.6f,\"k\":%d",
      fen.c_str(), d.v_cp, static_cast<double>(d.MeanCp()), e_u_cp_i8,
      static_cast<double>(d.u_mean), static_cast<double>(d.u_std), d.k);
  if (with_vstar) {
    std::printf(",\"v_star\":%d,\"u\":%d,\"depth\":%d,\"nodes\":%llu,\"decisive\":%s",
                v_star, u, reached_depth,
                static_cast<unsigned long long>(nodes),
                (std::abs(v_star) >= kDecisiveScoreThreshold) ? "true" : "false");
  }
  const char* names[] = {"pi", "mu", "sigma", "df"};
  const float* arrs[] = {d.pi, d.mu, d.sigma, d.df};
  for (int a = 0; a < 4; ++a) {
    std::printf(",\"%s\":[", names[a]);
    for (int i = 0; i < d.k; ++i) {
      std::printf("%s%.8g", i ? "," : "", static_cast<double>(arrs[a][i]));
    }
    std::printf("]");
  }
  std::printf(",\"q\":{");
  bool first_q = true;
  for (double tau : kTaus) {
    std::printf("%s\"%.2f\":%.6f", first_q ? "" : ",", tau,
                static_cast<double>(d.QuantileCp(static_cast<float>(tau))));
    first_q = false;
  }
  std::printf("}}\n");
}

static auto RunUncHarness(const string& net_path, bool with_vstar, int depth,
                          uint64_t node_cap) -> int {
  if (!g_nnue.Load(net_path)) {
    cerr << "unc_harness: failed to load net " << net_path << endl;
    return 1;
  }
  if (!g_nnue.HasHead()) {
    cerr << "unc_harness: " << net_path
         << " has no uncertainty head (need a fused OZNU net)" << endl;
    return 1;
  }
  cerr << "unc_harness: loaded head run_id=" << g_nnue.GetHeadRunId() << endl;

  SearchParams params;
  if (with_vstar) {
    // Match datagen's search profile (nnue since a net is loaded) so v_star is
    // comparable to the head's training labels.
    params = LoadParamsOrDie("params.json", ProfileForEvalMode());
    cerr << "unc_harness: v_star = fixed depth " << depth << ", node cap "
         << node_cap << endl;
  }

  string fen;
  while (getline(cin, fen)) {
    if (fen.empty()) {
      continue;
    }
    try {
      Board board(fen);
      UncDist d = board.GetUncDistribution();
      int e_u_cp_i8 = board.GetMeanCorrectionCp();  // int8 fast path (unc-008 G)
      int v_star = 0;
      int u = 0;
      int reached_depth = 0;
      uint64_t nodes = 0;
      if (with_vstar) {
        v_star = SearchVStar(board, params, depth, node_cap, reached_depth, nodes);
        u = d.v_cp - v_star;
      }
      PrintDistJson(fen, d, e_u_cp_i8, with_vstar, v_star, u, reached_depth,
                    nodes);
    } catch (const std::exception& e) {
      std::printf("{\"fen\":\"%s\",\"error\":\"%s\"}\n", fen.c_str(), e.what());
    }
  }
  return 0;
}

}  // namespace omegazero

auto main(int argc, char* argv[]) -> int {
  std::string net = "unc_research/models/nnue_unc.bin";
  bool with_vstar = false;
  int depth = 12;
  uint64_t node_cap = 2000000;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--vstar") {
      with_vstar = true;
    } else if (a == "--depth" && i + 1 < argc) {
      depth = std::atoi(argv[++i]);
    } else if (a == "--nodes" && i + 1 < argc) {
      node_cap = std::strtoull(argv[++i], nullptr, 10);
    } else if (!a.empty() && a[0] != '-') {
      net = a;
    } else {
      std::cerr << "unc_harness: unknown option " << a << std::endl;
      return 2;
    }
  }
  return omegazero::RunUncHarness(net, with_vstar, depth, node_cap);
}
