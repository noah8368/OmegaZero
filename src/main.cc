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
using std::invalid_argument;
using std::runtime_error;
using std::string;
using std::to_string;

auto main(int argc, char* argv[]) -> int {
  // Compute the default path for the opening book.
  string opening_book_path(argv[0]);
  constexpr size_t kProgNameLen = 9;
  opening_book_path.erase(opening_book_path.length() - kProgNameLen);
  opening_book_path += "../p3ECO.txt";

  // Parse optional arguments for testing and specifying initial position.
  namespace prog_opt = boost::program_options;
  prog_opt::options_description desc("Options");
  string init_pos;
  string game_record_file;
  float search_time;
  int depth;
  char player_side;
  desc.add_options()(
      "initial-position,i",
      prog_opt::value<string>(&init_pos)->default_value(
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
      "FEN formatted string specifying the initial game position")(
      "depth,d", prog_opt::value<int>(&depth),
      "Depth to run Perft testing function to")(
      "player-side,p", prog_opt::value<char>(&player_side)->default_value('w'),
      "Side user will play")(
      "time,t", prog_opt::value<float>(&search_time)->default_value(5),
      "Search time")("opening-book-path,o",
                     prog_opt::value<string>(&opening_book_path),
                     "Opening book file path")(
      "save,s", prog_opt::value<string>(&game_record_file),
      "File to save the move history to after a game is finished.");
  prog_opt::variables_map var_map;
  try {
    prog_opt::store(prog_opt::parse_command_line(argc, argv, desc), var_map);
    prog_opt::notify(var_map);
  } catch (prog_opt::error& e) {
    cout << "ERROR: Parsing fault: " << e.what() << endl;
    return EINVAL;
  }

  try {
    bool on_opening =
        init_pos == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    omegazero::Game game(init_pos, opening_book_path, player_side, search_time,
                         on_opening);
    if (var_map.count("depth")) {
      // Output perft results.
      game.Test(depth);
    } else {
      // Play a game against a user.
      while (game.IsActive()) {
        game.Play();
      }
      game.OutputWinner();

      if (var_map.count("save")) {
        game.Save(game_record_file);
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