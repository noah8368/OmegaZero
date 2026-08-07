/* Noah Himed
 *
 * Use a Game object to manage moves in a Chess game or test the engine.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "game.h"
#include "move.h"
#include "nnue.h"
#include "params.h"
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
       << "  --tc TIME      Clock time in seconds (enables timed game)\n"
       << "  --inc TIME     Increment in seconds (default: 0)\n"
       << "  -i FEN         Initial position as FEN string\n"
       << "  -o PATH        Opening book file path\n"
       << "  -n PATH        NNUE weights file path\n"
       << "  --pgn NAME     Save game as PGN with given opponent name\n"
       << "  --uci          Run in UCI protocol mode\n"
       << "  --hce          Use handcrafted eval instead of NNUE\n"
       << "  --dump-params  Write default params.json (both profiles) and exit\n"
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
  string params_path = exe_dir + "../params.json";
  string pgn_opponent;
  float search_time = 5.0f;
  float clock_time = 0.0f;
  float increment = 0.0f;
  char player_side = 'w';
  bool uci_mode = false;
  bool hce_mode = false;
  bool light_theme = false;
  bool dump_params = false;
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
    } else if (arg == "--dump-params") {
      dump_params = true;
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
    } else if (arg == "--pgn" && i + 1 < argc) {
      pgn_opponent = argv[++i];
    } else {
      cout << "Unknown option: " << arg << endl;
      PrintUsage(argv[0]);
      return EINVAL;
    }
  }

  // Regenerate params.json with both profiles at the built-in defaults. Kept in
  // sync with the parameter registry so the file never has to be hand-edited.
  if (dump_params) {
    omegazero::SearchParams defaults;
    if (omegazero::WriteParamsJson(
            params_path, {{"nnue", defaults}, {"hce", defaults}})) {
      cout << "Wrote default parameters to " << params_path << endl;
      return 0;
    }
    cout << "ERROR: could not write " << params_path << endl;
    return EIO;
  }

  if (hce_mode) {
    if (!uci_mode) cout << "Using HCE." << endl;
  } else if (!omegazero::g_nnue.Load(nnue_path)) {
    if (!uci_mode)
      cout << "WARNING: NNUE weights not found. Using HCE instead." << endl;
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
    omegazero::SearchParams params;
    omegazero::LoadProfileInto(params_path, omegazero::ProfileForEvalMode(),
                               params);
    game.SetSearchParams(params);

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
