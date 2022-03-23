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
      ("opening-book-path,o", prog_opt::value<string>(&opening_book_path),
       "Opening book file path")
       ("compare,c", "Have two engines play against each other");
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
      omegazero::Game white_engine_game(init_pos, opening_book_path, 'w',
                                        search_time);
      omegazero::Game black_engine_game(init_pos, opening_book_path, 'b',
                                        search_time);

      // Allow the two engines to play a game.
      omegazero::S8 next_player =
          (player_side == 'w') ? omegazero::kWhite : omegazero::kBlack;
      omegazero::Move white_engine_move;
      omegazero::Move black_engine_move;
      for (;;) {
        if (next_player == omegazero::kWhite) {
          white_engine_move = white_engine_game.MakeEngineMove();
          if (!(white_engine_game.IsActive() && black_engine_game.IsActive())) {
            break;
          }
          black_engine_game.MakeOtherEngineMove(white_engine_move);
        } else {
          black_engine_move = black_engine_game.MakeEngineMove();
          if (!(white_engine_game.IsActive() && black_engine_game.IsActive())) {
            break;
          }
          white_engine_game.MakeOtherEngineMove(black_engine_move);
        }
        next_player = omegazero::GetOtherPlayer(next_player);
        if (!(white_engine_game.IsActive() && black_engine_game.IsActive())) {
          break;
        }
      }

      // Output game results.
      omegazero::S8 winner = (white_engine_game.GetWinner() == omegazero::kNA)
                                 ? black_engine_game.GetWinner()
                                 : white_engine_game.GetWinner();
      if (winner == omegazero::kWhite) {
        cout << "WHITE ENGINE WINS" << endl;
      } else if (winner == omegazero::kBlack) {
        cout << "BLACK ENGINE WINS" << endl;
      } else {
        cout << "DRAW" << endl;
      }
      exit(EXIT_SUCCESS);
    }

    omegazero::Game game(init_pos, opening_book_path, player_side,
                         search_time);
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