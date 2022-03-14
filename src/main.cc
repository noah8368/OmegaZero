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
#include <vector>

#include "board.h"
#include "game.h"
#include "move.h"

using std::cout;
using std::endl;
using std::invalid_argument;
using std::runtime_error;
using std::string;
using std::vector;

auto main(int argc, char* argv[]) -> int {
  // Parse optional arguments for testing and specifying initial position.
  namespace prog_opt = boost::program_options;
  prog_opt::options_description desc("Options");
  vector<string> weight_paths;
  string init_pos;
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
      "Search time")(
      "compare,c",
      prog_opt::value<vector<std::string>>(&weight_paths)->multitoken(),
      "evaluation test model(s) weights' file path");
  prog_opt::variables_map var_map;
  try {
    prog_opt::store(prog_opt::parse_command_line(argc, argv, desc), var_map);
    prog_opt::notify(var_map);
  } catch (prog_opt::error& e) {
    cout << "ERROR: Parsing fault: " << e.what() << endl;
    return EINVAL;
  }

  try {
    if (var_map.count("compare")) {
      if (weight_paths.size() == 0 || weight_paths.size() > 2) {
        throw invalid_argument("Number of weight paths entered must be 1 or 2");
      }

      string test_engine_weights_path = weight_paths[0];
      string baseline_engine_weights_path;
      if (weight_paths.size() == 1) {
        baseline_engine_weights_path = "-";
      } else {
        baseline_engine_weights_path = weight_paths[1];
      }

      omegazero::S8 baseline_engine_player =
          (player_side == 'w') ? omegazero::kWhite : omegazero::kBlack;
      omegazero::S8 test_engine_player =
          omegazero::GetOtherPlayer(baseline_engine_player);

      char baseline_engine_side = player_side;
      char test_engine_side = (baseline_engine_side == 'w') ? 'b' : 'w';
      omegazero::Game baseline_engine_game(init_pos, baseline_engine_side,
                                           search_time,
                                           baseline_engine_weights_path);
      omegazero::Game test_engine_game(init_pos, test_engine_side, search_time,
                                       test_engine_weights_path);

      // Allow the two engines to play a game.
      omegazero::S8 next_player = omegazero::kWhite;
      omegazero::Move baseline_engine_move;
      omegazero::Move test_engine_move;
      for (;;) {
        if (next_player == baseline_engine_player) {
          baseline_engine_move = baseline_engine_game.MakeEngineMove();
          if (!(baseline_engine_game.IsActive() &&
                test_engine_game.IsActive())) {
            break;
          }
          test_engine_game.MakeOtherEngineMove(baseline_engine_move);
        } else {
          test_engine_move = test_engine_game.MakeEngineMove();
          if (!(baseline_engine_game.IsActive() &&
                test_engine_game.IsActive())) {
            break;
          }
          baseline_engine_game.MakeOtherEngineMove(test_engine_move);
        }
        next_player = omegazero::GetOtherPlayer(next_player);
        if (!(baseline_engine_game.IsActive() && test_engine_game.IsActive())) {
          break;
        }
      }
      omegazero::S8 winner = baseline_engine_game.GetWinner();
      if (winner == baseline_engine_player) {
        cout << "BASELINE ENGINE WINS" << endl;
      } else if (winner == test_engine_player) {
        cout << "TEST ENGINE WINS" << endl;
      } else {
        cout << "DRAW" << endl;
      }
      exit(EXIT_SUCCESS);
    }

    omegazero::Game game(init_pos, player_side, search_time);
    if (var_map.count("depth")) {
      // Output perft results.
      game.Test(depth);
    } else {
      // Play a game against a user.
      while (game.IsActive()) {
        game.Play();
      }
      game.OutputWinner();
    }
  } catch (invalid_argument& e) {
    cout << "ERROR: Invalid argument: " << e.what() << endl;
    exit(EINVAL);
  } catch (runtime_error& e) {
    cout << "ERROR: Unexpected problem encountered with " << e.what() << endl;
    exit(EXIT_FAILURE);
  }
}
