/* Noah Himed
 *
 * Automated test harness: perft regression, eval sanity, search sanity.
 * Build with: make test
 * Run with:   ./build/test_harness
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <cstdlib>
#include <iostream>
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
using std::string;
using std::vector;

int pass_count = 0;
int fail_count = 0;

string MoveToUci(const Move& m, S8 player) {
  if (m.castling_type == kQueenSide) return player == kWhite ? "e1c1" : "e8c8";
  if (m.castling_type == kKingSide)  return player == kWhite ? "e1g1" : "e8g8";
  string s;
  s += static_cast<char>('a' + GetFileFromSq(m.start_sq));
  s += static_cast<char>('1' + GetRankFromSq(m.start_sq));
  s += static_cast<char>('a' + GetFileFromSq(m.target_sq));
  s += static_cast<char>('1' + GetRankFromSq(m.target_sq));
  if (m.promoted_to_piece != kNA) {
    // Piece enum: kPawn=0, kKnight=1, kBishop=2, kRook=3, kQueen=4, kKing=5
    static const char kPromo[] = {0, 'n', 'b', 'r', 'q', 0};
    s += kPromo[m.promoted_to_piece];
  }
  return s;
}

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
    S8 player = board2.GetPlayerToMove();
    vector<Move> moves = engine2.GenerateMoves();
    for (Move& m : moves) {
      try {
        board2.MakeMove(m);
      } catch (BadMove&) {
        continue;
      }
      U64 sub = engine2.Perft(test_case.depth - 1);
      board2.UnmakeMove(m);
      cout << "    " << MoveToUci(m, player) << ": " << sub << endl;
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
  Move m;
  try {
    m = engine.GetBestMove();
  } catch (const std::exception& e) {
    cout << "FAIL [" << test_case.name << "] threw: " << e.what() << "\n"
         << "  FEN: " << test_case.fen << endl;
    ++fail_count;
    return;
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

}  // namespace omegazero

auto main() -> int {
  using namespace omegazero;

  cout << "=== Perft ===" << endl;
  for (const auto& test_case : kPerftCases) RunPerft(test_case);

  cout << "\n=== Eval ===" << endl;
  for (const auto& test_case : kEvalCases) RunEval(test_case);

  cout << "\n=== Search ===" << endl;
  for (const auto& test_case : kSearchCases) RunSearch(test_case);

  int total = pass_count + fail_count;
  cout << "\n" << total << " tests: " << pass_count << " passed, "
       << fail_count << " failed" << endl;
  return fail_count > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
