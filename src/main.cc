/* Noah Himed
 *
 * Use a Game object to manage moves in a Chess game or test the engine.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "board.h"
#include "game.h"
#include "nnue.h"
#include "params.h"
#include "syzygy.h"
#include "uci.h"

using std::cout;
using std::endl;
using std::invalid_argument;
using std::runtime_error;
using std::string;

static void PrintUsage(const char* prog) {
  cout << "Usage: " << prog << " [OPTIONS]\n"
       << "  -p SIDE        Side to play: w, b, or r (default: w)\n"
       << "  --st TIME      Search time per move in seconds (default: 5)\n"
       << "  --threads N    Number of search threads (Lazy SMP; default: all "
          "cores)\n"
       << "  --syzygy DIR   Syzygy tablebase directory (default: repo root)\n"
       << "  --tc TIME      Clock time in seconds (enables timed game)\n"
       << "  --inc TIME     Increment in seconds (default: 0)\n"
       << "  -i FEN         Initial position as FEN string\n"
       << "  -o PATH        Opening book file path\n"
       << "  -n PATH        NNUE weights file path\n"
       << "  --pgn NAME     Save game as PGN with given opponent name\n"
       << "  --uci          Run in UCI protocol mode\n"
       << "  --hce          Use handcrafted eval instead of NNUE\n"
       << "  --light-theme  Piece symbols for light terminal backgrounds\n"
       << "  --help         Show this message\n";
}

auto main(int argc, char* argv[]) -> int {
  string exe_dir(argv[0]);
  constexpr size_t kProgNameLen = 9;
  exe_dir.erase(exe_dir.length() - kProgNameLen);

  string init_pos = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  string opening_book_path = exe_dir + "../openings.pgn";
  string nnue_path = exe_dir + "../nnue/nnue.bin";
  string syzygy_path =
      exe_dir + "../syzygy_tables";  // where setup.sh puts the tables
  string params_path = exe_dir + "../params.json";
  string pgn_opponent;
  float search_time = 5.0f;
  float clock_time = 0.0f;
  float increment = 0.0f;
  char player_side = 'w';
  bool uci_mode = false;
  bool hce_mode = false;
  bool light_theme = false;
  bool unc_probe = false;
  int num_threads = omegazero::DefaultThreadCount();

  for (int i = 1; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg == "--uci" || arg == "-u") {
      uci_mode = true;
    } else if (arg == "--hce") {
      hce_mode = true;
    } else if (arg == "--light-theme") {
      light_theme = true;
    } else if ((arg == "-p" || arg == "--player-side") && i + 1 < argc) {
      player_side = argv[++i][0];
    } else if (arg == "--st" && i + 1 < argc) {
      search_time = std::atof(argv[++i]);
    } else if (arg == "--threads" && i + 1 < argc) {
      int t = std::atoi(argv[++i]);
      num_threads =
          t < 1 ? 1 : (t > omegazero::kMaxThreads ? omegazero::kMaxThreads : t);
    } else if (arg == "--tc" && i + 1 < argc) {
      clock_time = std::atof(argv[++i]);
    } else if (arg == "--inc" && i + 1 < argc) {
      increment = std::atof(argv[++i]);
    } else if ((arg == "-i" || arg == "--initial-position") && i + 1 < argc) {
      init_pos = argv[++i];
    } else if ((arg == "-o" || arg == "--opening-book-path") && i + 1 < argc) {
      opening_book_path = argv[++i];
    } else if ((arg == "-n" || arg == "--nnue") && i + 1 < argc) {
      nnue_path = argv[++i];
    } else if (arg == "--syzygy" && i + 1 < argc) {
      syzygy_path = argv[++i];
    } else if (arg == "--pgn" && i + 1 < argc) {
      pgn_opponent = argv[++i];
    } else if (arg == "--unc-probe") {
      unc_probe = true;
    } else {
      cout << "Unknown option: " << arg << endl;
      PrintUsage(argv[0]);
      return EINVAL;
    }
  }

  if (hce_mode) {
    if (!uci_mode) cout << "Using HCE." << endl;
  } else if (!omegazero::g_nnue.Load(nnue_path)) {
    if (!uci_mode)
      cout << "WARNING: NNUE weights not found. Using HCE instead." << endl;
  } else if (omegazero::g_nnue.HasHead() && !uci_mode) {
    cout << "NNUE: fused OZNU with uncertainty head (run_id="
         << omegazero::g_nnue.GetHeadRunId() << ")." << endl;
  }

  // Uncertainty probe: read FENs from stdin, print p(u|x) per position as JSON
  // (one line each). The reference the A4 parity gate compares against, and the
  // seed of the unc-007 harness (B). Requires a fused OZNU net.
  if (unc_probe) {
    if (!omegazero::g_nnue.HasHead()) {
      cout << "unc-probe: net has no uncertainty head" << endl;
      return EINVAL;
    }
    const double taus[] = {0.01, 0.05, 0.1, 0.25, 0.5, 0.75, 0.9, 0.95, 0.99};
    string fen;
    while (std::getline(std::cin, fen)) {
      if (fen.empty()) {
        continue;
      }
      try {
        omegazero::Board board(fen);
        omegazero::UncDist d = board.GetUncDistribution();
        std::printf(
            "{\"fen\":\"%s\",\"v\":%d,\"e_u_cp\":%.6f,\"u_mean\":%.6f,"
            "\"u_std\":%.6f,\"k\":%d",
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
        for (double tau : taus) {
          std::printf("%s\"%.2f\":%.6f", first_q ? "" : ",", tau,
                      static_cast<double>(d.QuantileCp(static_cast<float>(tau))));
          first_q = false;
        }
        std::printf("}}\n");
      } catch (const std::exception& e) {
        std::printf("{\"fen\":\"%s\",\"error\":\"%s\"}\n", fen.c_str(), e.what());
      }
    }
    return 0;
  }

  // Load Syzygy endgame tablebases (default: the repo root; override --syzygy).
  // A no-op when no .rtbw/.rtbz files are present at the path.
  if (omegazero::g_syzygy.Init(syzygy_path) && !uci_mode) {
    cout << "Syzygy: " << omegazero::g_syzygy.MaxPieces()
         << "-man tablebases loaded." << endl;
  }

  if (uci_mode) {
    omegazero::UciHandler uci(opening_book_path, params_path);
    uci.Run();
    return 0;
  }

  try {
    bool on_opening =
        init_pos == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    omegazero::Game game(init_pos, opening_book_path, player_side, search_time,
                         on_opening, light_theme, num_threads);

    // Apply the params.json profile matching the eval mode set above.
    game.SetSearchParams(omegazero::LoadParamsOrDie(
        params_path, omegazero::ProfileForEvalMode()));

    if (clock_time > 0.0f) {
      game.SetClock(clock_time, increment);
    }

    while (game.IsActive()) {
      game.Play();
    }
    game.OutputWinner();

    if (!pgn_opponent.empty()) {
      game.SavePgn(pgn_opponent);
    }
  } catch (invalid_argument& e) {
    cout << "ERROR: Invalid argument: " << e.what() << endl;
    exit(EINVAL);
  } catch (runtime_error& e) {
    cout << "ERROR: Unexpected problem encountered with " << e.what() << endl;
    exit(EXIT_FAILURE);
  }
}
