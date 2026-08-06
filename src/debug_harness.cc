/* Noah Himed
 *
 * Self-play crash detection harness.
 * Build with: make debug
 * Run with:   ./build/debug_harness [num_games] [search_time]
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "engine.h"
#include "move.h"

namespace omegazero {

using std::cout;
using std::endl;
using std::ofstream;
using std::ostringstream;
using std::string;
using std::to_string;
using std::vector;

static const char kPieceLetters[] = {0, 'N', 'B', 'R', 'Q', 'K'};

string MoveToFide(const Move& m, const Board& board) {
  if (m.castling_type == kQueenSide) return "0-0-0";
  if (m.castling_type == kKingSide)  return "0-0";

  string s;
  S8 moving_player = board.GetPlayerToMove();
  S8 start_file = GetFileFromSq(m.start_sq);
  S8 start_rank = GetRankFromSq(m.start_sq);
  S8 target_file = GetFileFromSq(m.target_sq);
  S8 target_rank = GetRankFromSq(m.target_sq);

  if (m.moving_piece == kPawn) {
    if (m.captured_piece != kNA || m.is_ep) {
      s += static_cast<char>('a' + start_file);
      s += 'x';
    }
  } else {
    s += kPieceLetters[m.moving_piece];
    Bitboard candidates = board.GetAttackMap(moving_player, m.target_sq, m.moving_piece)
                        & board.GetPiecesByType(m.moving_piece, moving_player);
    if (!OneSqSet(candidates)) {
      if (OneSqSet(candidates & kRankMasks[start_rank])) {
        s += static_cast<char>('1' + start_rank);
      } else if (OneSqSet(candidates & kFileMasks[start_file])) {
        s += static_cast<char>('a' + start_file);
      } else {
        s += static_cast<char>('a' + start_file);
        s += static_cast<char>('1' + start_rank);
      }
    }
    if (m.captured_piece != kNA) s += 'x';
  }

  s += static_cast<char>('a' + target_file);
  s += static_cast<char>('1' + target_rank);

  if (m.promoted_to_piece != kNA) {
    s += kPieceLetters[m.promoted_to_piece];
  } else if (m.is_ep) {
    s += "e.p.";
  }
  return s;
}

string BoardToString(const Board& board) {
  static const char kPieceChars[] = {'P', 'N', 'B', 'R', 'Q', 'K'};
  ostringstream s;
  for (int rank = 7; rank >= 0; --rank) {
    s << (rank + 1) << " ";
    for (int file = 0; file < 8; ++file) {
      S8 sq = GetSqFromRankFile(static_cast<S8>(rank), static_cast<S8>(file));
      S8 piece = board.GetPieceOnSq(sq);
      if (piece == kNA) {
        s << ". ";
      } else {
        char c = kPieceChars[piece];
        if (board.GetPlayerOnSq(sq) == kBlack) c += 'a' - 'A';
        s << c << " ";
      }
    }
    s << "\n";
  }
  s << "  a b c d e f g h\n";
  s << (board.GetPlayerToMove() == kWhite ? "White" : "Black") << " to move";
  return s.str();
}

struct SuppressCout {
  ofstream dev_null_;
  std::streambuf* saved_;
  SuppressCout() : dev_null_("/dev/null"), saved_(cout.rdbuf(dev_null_.rdbuf())) {}
  ~SuppressCout() { cout.rdbuf(saved_); }
};

string FormatMoveHistory(const vector<string>& move_history) {
  ostringstream s;
  for (size_t i = 0; i < move_history.size(); ++i) {
    if (i % 2 == 0) s << (i / 2 + 1) << ". ";
    s << move_history[i] << " ";
  }
  return s.str();
}

bool RunSelfPlay(int num_games, float search_time, const string& out_dir) {
  constexpr int kMaxMovesPerGame = 200;
  const string kStartFen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  for (int game = 1; game <= num_games; ++game) {
    cout << "  Game " << game << "/" << num_games << " ..." << std::flush;

    Board board(kStartFen);
    TranspositionTable tt;
    Engine engine(&tt, &board, 'w', search_time);
    vector<string> move_history;
    string error_msg;

    for (int half_move = 0; half_move < kMaxMovesPerGame; ++half_move) {
      S8 status = engine.GetGameStatus();
      if (status == kPlayerCheckmated || status == kDraw) break;

      Move m;
      engine.AddPosToHistory();
      {
        SuppressCout suppress;
        try {
          m = engine.GetBestMove();
        } catch (const std::exception& e) {
          error_msg = "GetBestMove threw after " +
                      to_string(move_history.size()) + " moves: " + e.what();
          break;
        }
      }

      if (m.IsEmpty()) {
        error_msg = "engine returned empty move after " +
                    to_string(move_history.size()) + " move(s) played";
        break;
      }

      move_history.push_back(MoveToFide(m, board));

      try {
        board.MakeMove(m);
      } catch (BadMove& e) {
        error_msg = "MakeMove rejected move " + move_history.back() +
                    " after " + to_string(move_history.size()) +
                    " moves: " + e.what();
        break;
      } catch (const std::exception& e) {
        error_msg = "MakeMove threw after " +
                    to_string(move_history.size()) + " moves: " + e.what();
        break;
      }
    }

    string moves = FormatMoveHistory(move_history);

    if (error_msg.empty()) {
      cout << " ok (" << move_history.size() << " half-moves)" << endl;
      cout << "  " << moves << "\n" << endl;
      continue;
    }

    cout << " ERROR" << endl;
    string board_str = BoardToString(board);
    cout << "  Error: " << error_msg << "\n"
         << "  " << moves << "\n"
         << board_str << "\n" << endl;

    auto now = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    std::tm* lt = std::localtime(&now_t);
    ostringstream ts;
    ts << std::setfill('0')
       << (lt->tm_year + 1900) << "-"
       << std::setw(2) << (lt->tm_mon + 1) << "-"
       << std::setw(2) << lt->tm_mday << "_"
       << std::setw(2) << lt->tm_hour << "-"
       << std::setw(2) << lt->tm_min << "-"
       << std::setw(2) << lt->tm_sec;
    string crash_log = out_dir + "crash_log_" + ts.str() + ".txt";

    ofstream f(crash_log);
    if (f) {
      f << "Game " << game << " ERROR: " << error_msg << "\n"
        << moves << "\n\n"
        << board_str << "\n";
      cout << "Saved to " << crash_log << endl;
    } else {
      cout << "(could not write " << crash_log << ")" << endl;
    }

    return false;
  }

  return true;
}

}  // namespace omegazero

auto main(int argc, char* argv[]) -> int {
  using namespace omegazero;

  string out_dir(argv[0]);
  size_t last_slash = out_dir.rfind('/');
  out_dir = (last_slash != string::npos) ? out_dir.substr(0, last_slash + 1)
                                         : "./";

  int num_games = 1;
  float search_time = 0.1f;
  if (argc > 1) num_games = std::atoi(argv[1]);
  if (argc > 2) search_time = std::atof(argv[2]);

  cout << "=== Self-play ===" << endl;
  cout << "  Games: " << num_games << "  Search time: " << search_time << "s" << endl;
  bool ok = RunSelfPlay(num_games, search_time, out_dir);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
