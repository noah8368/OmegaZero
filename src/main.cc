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
  float search_time;
  int depth;
  char player_side;
  desc.add_options()
      ("initial-position,i",
      prog_opt::value<std::string>(&init_pos)->default_value(
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
      "FEN formatted string specifying the initial game position")
      ("depth,d", prog_opt::value<int>(&depth),
       "Depth to run Perft testing function to")
      ("stats,s", "Output search function statistics")
      ("player-side,p", prog_opt::value<char>(&player_side)->default_value('w'),
       "Side user will play")
      ("time,t", prog_opt::value<float>(&search_time)->default_value(5),
       "Search time");
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
    omegazero::Game game(init_pos, player_side, search_time);
    if (var_map.count("depth")) {
      game.Test(depth);
    } else if (var_map.count("stats")) {
      game.TimeSearch();
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
