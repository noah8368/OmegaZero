/* Noah Himed
 *
 * Uncertainty-head harness (unc-007). Reads FENs from stdin, one per line, and
 * for each prints the fused net's eval v and the predicted eval-error
 * distribution p(u | x) as a single JSON line: v, E[u|x], the Student-t mixture
 * params, and quantiles. This is the ONLY place the uncertainty head is called
 * from a binary; the engine (main) stays free of head code.
 *
 * Build with: make unc_harness
 * Run with:   build/unc_harness [net_path] < fens.txt   (default net: the fused
 *             unc_research/models/nnue_unc.bin)
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

#include "board.h"
#include "nnue.h"

namespace omegazero {

using std::cerr;
using std::cin;
using std::endl;
using std::getline;
using std::string;

// Quantile levels emitted per position (one-sided margin reads live in the tail).
static const double kTaus[] = {0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99};

static auto PrintDistJson(const string& fen, const UncDist& d) -> void {
  std::printf(
      "{\"fen\":\"%s\",\"v\":%d,\"e_u_cp\":%.6f,\"u_mean\":%.6f,\"u_std\":%.6f,"
      "\"k\":%d",
      fen.c_str(), d.v_cp, static_cast<double>(d.MeanCp()),
      static_cast<double>(d.u_mean), static_cast<double>(d.u_std), d.k);
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

static auto RunUncHarness(const string& net_path) -> int {
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

  string fen;
  while (getline(cin, fen)) {
    if (fen.empty()) {
      continue;
    }
    try {
      Board board(fen);
      PrintDistJson(fen, board.GetUncDistribution());
    } catch (const std::exception& e) {
      std::printf("{\"fen\":\"%s\",\"error\":\"%s\"}\n", fen.c_str(), e.what());
    }
  }
  return 0;
}

}  // namespace omegazero

auto main(int argc, char* argv[]) -> int {
  std::string net = "unc_research/models/nnue_unc.bin";
  if (argc > 1) {
    net = argv[1];
  }
  return omegazero::RunUncHarness(net);
}
