/* Noah Himed
 *
 * Automated test harness: perft regression, eval sanity, search sanity,
 * and self-play crash detection.
 * Build with: make test
 * Run with:   ./build/test_harness
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
#include <stdexcept>
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

int pass_count = 0;
int fail_count = 0;

// Piece enum: kPawn=0, kKnight=1, kBishop=2, kRook=3, kQueen=4, kKing=5
static const char kPieceLetters[] = {0, 'N', 'B', 'R', 'Q', 'K'};

// Generates a FIDE algebraic notation string for a move. Must be called with
// the board state BEFORE the move is applied (needed for disambiguation).
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
    // Disambiguate if another piece of the same type can reach the same square.
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

// Redirects cout to /dev/null for its lifetime. Used to suppress the engine's
// "SEARCH DEPTH:" diagnostic during self-play so it doesn't drown the output.
struct SuppressCout {
  ofstream dev_null_;
  std::streambuf* saved_;
  SuppressCout() : dev_null_("/dev/null"), saved_(cout.rdbuf(dev_null_.rdbuf())) {}
  ~SuppressCout() { cout.rdbuf(saved_); }
};

// ---- Perft ----

struct PerftCase {
  const char* name;
  const char* fen;
  int depth;
  U64 expected;
};

// Source: Chess Programming Wiki perft test suite.
const PerftCase kPerftCases[] = {
  // Initial position.
  {"initial/d1", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20},
  {"initial/d2", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2, 400},
  {"initial/d3", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902},
  {"initial/d4", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
  {"initial/d5", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
  // Kiwipete -- stresses castling, EP, and discovered checks.
  {"kiwipete/d1", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48},
  {"kiwipete/d2", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, 2039},
  {"kiwipete/d3", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
  {"kiwipete/d4", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
  // Position 3 -- stresses checks and pins.
  {"pos3/d1", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 1, 14},
  {"pos3/d2", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 2, 191},
  {"pos3/d3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3, 2812},
  {"pos3/d4", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
  {"pos3/d5", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
  // Position 5 -- stresses promotions.
  {"pos5/d1", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 1, 44},
  {"pos5/d2", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 2, 1486},
  {"pos5/d3", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
};

void RunPerft(const PerftCase& test_case) {
  Board board(test_case.fen);
  Engine engine(&board, 'w', 0.5f);
  U64 node_count = 0;
  try {
    node_count = engine.Perft(test_case.depth);
  } catch (const std::exception& e) {
    cout << "FAIL [" << test_case.name << "] threw: " << e.what() << "\n"
         << "  FEN: " << test_case.fen << endl;
    ++fail_count;
    return;
  }
  if (node_count == test_case.expected) {
    cout << "PASS [" << test_case.name << "]" << endl;
    ++pass_count;
    return;
  }
  cout << "FAIL [" << test_case.name << "]\n"
       << "  FEN:      " << test_case.fen << "\n"
       << "  Expected: " << test_case.expected << "  Got: " << node_count << endl;
  // Re-run one level shallower to show which move subtree is wrong.
  if (test_case.depth > 1) {
    cout << "  Per-move subtrees (depth " << (test_case.depth - 1) << "):" << endl;
    Board board2(test_case.fen);
    Engine engine2(&board2, 'w', 0.5f);
    vector<Move> moves = engine2.GenerateMoves();
    for (Move& m : moves) {
      string move_str = MoveToFide(m, board2);
      try {
        board2.MakeMove(m);
      } catch (BadMove&) {
        continue;
      }
      U64 sub = engine2.Perft(test_case.depth - 1);
      board2.UnmakeMove(m);
      cout << "    " << move_str << ": " << sub << endl;
    }
  }
  ++fail_count;
}

// ---- Eval sanity ----

struct EvalCase {
  const char* name;
  const char* fen;
  int min_eval;
  int max_eval;
};

// Score is from the perspective of the player to move.
const EvalCase kEvalCases[] = {
  // Starting position is symmetric -- expect roughly equal.
  {"eval/start_equal",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", -50, 50},
  // K+Q vs K: mover is up ~900cp.
  {"eval/white_queen_up", "8/8/4k3/8/8/4K3/8/3Q4 w - - 0 1", 700, 20000},
  {"eval/black_queen_up", "8/8/4k3/8/8/4K3/3q4/8 b - - 0 1", 700, 20000},
  // K+R vs K: mover is up ~500cp.
  {"eval/white_rook_up",  "8/8/4k3/8/8/4K3/8/4R3 w - - 0 1", 350, 20000},
};

void RunEval(const EvalCase& test_case) {
  Board board(test_case.fen);
  int eval;
  try {
    eval = board.Evaluate();
  } catch (const std::exception& e) {
    cout << "FAIL [" << test_case.name << "] threw: " << e.what() << "\n"
         << "  FEN: " << test_case.fen << endl;
    ++fail_count;
    return;
  }
  if (eval >= test_case.min_eval && eval <= test_case.max_eval) {
    cout << "PASS [" << test_case.name << "]" << endl;
    ++pass_count;
    return;
  }
  cout << "FAIL [" << test_case.name << "]\n"
       << "  FEN:      " << test_case.fen << "\n"
       << "  Expected: [" << test_case.min_eval << ", " << test_case.max_eval << "]"
       << "  Got: " << eval << endl;
  ++fail_count;
}

// ---- Search sanity ----

struct SearchCase {
  const char* name;
  const char* fen;
};

// Verify engine returns a valid move and doesn't crash.
// Extend these with forbidden_moves[] once eval bugs are addressed.
const SearchCase kSearchCases[] = {
  {"search/start",          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
  {"search/white_queen_up", "8/4k3/8/8/8/8/4K3/3Q4 w - - 0 1"},
  {"search/ep_available",   "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3"},
  {"search/castling",       "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1"},
};

void RunSearch(const SearchCase& test_case) {
  Board board(test_case.fen);
  Engine engine(&board, 'w', 0.1f);
  engine.AddPosToHistory();
  Move m;
  {
    SuppressCout suppress;
    try {
      m = engine.GetBestMove();
    } catch (const std::exception& e) {
      cout << "FAIL [" << test_case.name << "] threw: " << e.what() << "\n"
           << "  FEN: " << test_case.fen << endl;
      ++fail_count;
      return;
    }
  }
  if (m.moving_piece != kNA || m.castling_type != kNA) {
    cout << "PASS [" << test_case.name << "]" << endl;
    ++pass_count;
    return;
  }
  cout << "FAIL [" << test_case.name << "] engine returned empty move\n"
       << "  FEN: " << test_case.fen << endl;
  ++fail_count;
}

// ---- Self-play crash detection ----

string FormatMoveHistory(const vector<string>& move_history) {
  ostringstream s;
  for (size_t i = 0; i < move_history.size(); ++i) {
    if (i % 2 == 0) s << (i / 2 + 1) << ". ";
    s << move_history[i] << " ";
  }
  return s.str();
}

// Plays num_games games engine-vs-itself from the starting position. Prints
// each game's move history to cout and appends it to build/self_play_games.txt.
// Stops on the first error. Returns true if all games completed without error.
bool RunSelfPlay(int num_games, float search_time, const string& out_dir) {
  constexpr int kMaxMovesPerGame = 200;
  const string kStartFen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  for (int game = 1; game <= num_games; ++game) {
    cout << "  Game " << game << "/" << num_games << " ..." << std::flush;

    Board board(kStartFen);
    Engine engine(&board, 'w', search_time);
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

      if (m.moving_piece == kNA && m.castling_type == kNA) {
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


#ifdef BENCHMARK
// ---- NPS Benchmark ----
// Searches a set of positions for a fixed duration and reports average NPS.

struct BenchPos {
  const char* name;
  const char* fen;
};

const BenchPos kBenchPositions[] = {
  {"opening",  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
  {"midgame",  "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"},
  {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
  {"endgame",  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"},
};

void RunNpsBench(float search_time) {
  uint64_t total_nodes = 0;
  double total_elapsed = 0;

  for (const auto& pos : kBenchPositions) {
    Board board(pos.fen);
    Engine engine(&board, 'w', search_time);
    engine.AddPosToHistory();

    auto start = std::chrono::high_resolution_clock::now();
    engine.GetBestMove();
    auto end = std::chrono::high_resolution_clock::now();

    double elapsed = std::chrono::duration<double>(end - start).count();
    uint64_t nodes = engine.GetTotalNodes();
    uint64_t nps = elapsed > 0 ? static_cast<uint64_t>(nodes / elapsed) : 0;

    cout << "  " << pos.name << ": " << nodes << " nodes in "
         << std::fixed << std::setprecision(2) << elapsed << "s"
         << " (" << nps << " NPS)" << endl;

    total_nodes += nodes;
    total_elapsed += elapsed;
  }

  uint64_t avg_nps = total_elapsed > 0
      ? static_cast<uint64_t>(total_nodes / total_elapsed) : 0;
  cout << "  Average: " << avg_nps << " NPS" << endl;
}
#endif

int debug(const string& out_dir) {
  cout << "=== Perft ===" << endl;
  for (const auto& test_case : kPerftCases) RunPerft(test_case);

  cout << "\n=== Eval ===" << endl;
  for (const auto& test_case : kEvalCases) RunEval(test_case);

  cout << "\n=== Search ===" << endl;
  for (const auto& test_case : kSearchCases) RunSearch(test_case);

  int total = pass_count + fail_count;
  cout << "\n" << total << " tests: " << pass_count << " passed, "
       << fail_count << " failed" << endl;

  cout << "\n=== Self-play ===" << endl;
  bool self_play_ok = RunSelfPlay(1, 0.1f, out_dir);

  return (fail_count > 0 || !self_play_ok) ? EXIT_FAILURE : EXIT_SUCCESS;
}

#ifdef BENCHMARK
int benchmark() {
  cout << "=== NPS Benchmark (5s/position) ===" << endl;
  RunNpsBench(5.0f);
  return EXIT_SUCCESS;
}
#endif

}  // namespace omegazero

auto main(int, char* argv[]) -> int {
  using namespace omegazero;

  // Derive output directory from the executable path so files land next to the
  // binary regardless of which directory the harness is invoked from.
  string out_dir(argv[0]);
  size_t last_slash = out_dir.rfind('/');
  out_dir = (last_slash != string::npos) ? out_dir.substr(0, last_slash + 1)
                                         : "./";

#ifdef BENCHMARK
  return benchmark();
#else
  return debug(out_dir);
#endif
}
