/* Noah Himed
 *
 * Use a Game object to manage moves in a Chess game or test the engine.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <boost/program_options.hpp>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>

#include "game.h"
#include "move.h"
#include "nnue.h"
#include "uci.h"

using std::cout;
using std::endl;
using std::invalid_argument;
using std::runtime_error;
using std::string;
using std::to_string;

auto main(int argc, char* argv[]) -> int {
  // Compute default paths relative to the executable location.
  string exe_dir(argv[0]);
  constexpr size_t kProgNameLen = 9;
  exe_dir.erase(exe_dir.length() - kProgNameLen);

  string opening_book_path = exe_dir + "../p3ECO.txt";
  string nnue_path = exe_dir + "../nnue/nnue.bin";

  // Parse optional arguments for testing and specifying initial position.
  namespace prog_opt = boost::program_options;
  prog_opt::options_description desc("Options");
  string init_pos;
  string pgn_opponent;
  float search_time;
  int depth;
  char player_side;
  desc.add_options()
      ("initial-position,i",
      prog_opt::value<string>(&init_pos)->default_value(
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
      "FEN formatted string specifying the initial game position")
      ("depth,d", prog_opt::value<int>(&depth),
      "Depth to run Perft testing function to")
      ("player-side,p", prog_opt::value<char>(&player_side)->default_value('w'),
      "Side user will play")
      ("time,t", prog_opt::value<float>(&search_time)->default_value(5),
      "Search time")
      ("opening-book-path,o",
      prog_opt::value<string>(&opening_book_path),
      "Opening book file path")
      ("nnue,n", prog_opt::value<string>(&nnue_path),
      "NNUE weights file path")
      ("pgn", prog_opt::value<string>(&pgn_opponent),
      "Save game as PGN file, using the given opponent name")
      ("uci,u", "Run in UCI protocol mode")
      ("hce", "Use handcrafted eval instead of NNUE")
      ("light-theme", "Use piece symbols for light terminal backgrounds");
  prog_opt::variables_map var_map;
  try {
    prog_opt::store(prog_opt::parse_command_line(argc, argv, desc), var_map);
    prog_opt::notify(var_map);
  } catch (prog_opt::error& e) {
    cout << "ERROR: Parsing fault: " << e.what() << endl;
    return EINVAL;
  }

  bool uci_mode = var_map.count("uci") > 0;

  if (var_map.count("hce")) {
    if (!uci_mode) cout << "Using HCE." << endl;
  } else if (!omegazero::g_nnue.Load(nnue_path)) {
    if (!uci_mode) cout << "WARNING: NNUE weights not found. Using HCE instead." << endl;
  }

  if (uci_mode) {
    omegazero::UciHandler uci(opening_book_path);
    uci.Run();
    return 0;
  }

  try {
    bool on_opening =
        init_pos == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    bool light_theme = var_map.count("light-theme") > 0;
    omegazero::Game game(init_pos, opening_book_path, player_side, search_time,
                         on_opening, light_theme);
    if (var_map.count("depth")) {
      // Output perft results.
      game.Test(depth);
    } else {
      // Play a game against a user.
      while (game.IsActive()) {
        game.Play();
      }
      game.OutputWinner();

      if (var_map.count("pgn")) {
        game.SavePgn(pgn_opponent);
      }
    }
  } catch (invalid_argument& e) {
    cout << "ERROR: Invalid argument: " << e.what() << endl;
    exit(EINVAL);
  } catch (runtime_error& e) {
    cout << "ERROR: Unexpected problem encountered with " << e.what() << endl;
    exit(EXIT_FAILURE);
  }
}