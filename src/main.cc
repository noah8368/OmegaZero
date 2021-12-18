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

using std::cout;
using std::endl;

auto main(int argc, char* argv[]) -> int {
  // Parse optional arguments for testing and specifying initial position.
  namespace prog_opt = boost::program_options;
  prog_opt::options_description desc("Options");
  std::string init_pos;
  int depth;
  desc.add_options()(
      "initial-position,i",
      prog_opt::value<std::string>(&init_pos)->default_value(
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
      "FEN formatted string specifying the initial game position")(
      "test,t", prog_opt::value<int>(&depth),
      "Depth to run Perft testing function to")("stats,s",
                                                prog_opt::value<int>(&depth),
                                                "depth to use when timing "
                                                "search");
  prog_opt::variables_map var_map;
  try {
    prog_opt::store(prog_opt::parse_command_line(argc, argv, desc), var_map);
    prog_opt::notify(var_map);
  } catch (prog_opt::error& e) {
    cout << "ERROR: Parsing fault: " << e.what() << endl;
    return EINVAL;
  }

  // Initialize the engine and either test it or begin a game.
  try {
    omegazero::Game game(init_pos);
    if (var_map.count("test")) {
      game.Test(depth);
    } else if (var_map.count("stats")) {
      game.TimeSearch(depth);
    } else {
      while (game.IsActive()) {
        game.Play();
      }
      game.OutputWinner();
    }
  } catch (std::invalid_argument& e) {
    cout << "ERROR: Invalid argument: " << e.what() << endl;
    return EINVAL;
  }
}
