/* Noah Himed
 *
 * Search trace harness: runs the engine on a FEN position and outputs the
 * search tree as JSON for visualization.
 *
 * Build with: make trace
 * Run with:   ./build/trace_harness "FEN" [search_time]
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <iostream>
#include <string>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "nnue.h"
#include "search_trace.h"

namespace omegazero {

using std::cerr;
using std::cout;
using std::endl;
using std::string;

static string EscapeJson(const string& s) {
  string out;
  for (char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out += c;
  }
  return out;
}

static void PrintTraceNode(const TraceNode& node, int indent) {
  string pad(indent, ' ');
  cout << pad << "{" << endl;
  cout << pad << "  \"move\": \"" << EscapeJson(node.move_uci) << "\"," << endl;
  cout << pad << "  \"eval\": " << node.eval << "," << endl;
  cout << pad << "  \"pruned\": " << (node.pruned ? "true" : "false") << "," << endl;
  if (node.pruned) {
    cout << pad << "  \"prune_reason\": \"" << EscapeJson(node.prune_reason) << "\"," << endl;
  }
  cout << pad << "  \"is_pv\": " << (node.is_pv ? "true" : "false") << "," << endl;
  cout << pad << "  \"children\": [" << endl;
  for (size_t i = 0; i < node.children.size(); ++i) {
    PrintTraceNode(node.children[i], indent + 4);
    if (i + 1 < node.children.size()) cout << ",";
    cout << endl;
  }
  cout << pad << "  ]" << endl;
  cout << pad << "}";
}

}  // namespace omegazero

int main(int argc, char* argv[]) {
  using namespace omegazero;

  if (argc < 2) {
    cerr << "Usage: " << argv[0] << " \"FEN\" [search_time_sec]" << endl;
    return 1;
  }

  string fen = argv[1];
  float search_time = 1.0f;
  if (argc >= 3) {
    search_time = std::stof(argv[2]);
  }

  Board board(fen);
  Engine engine(&board, 'w', search_time);
  engine.AddPosToHistory();

  int score = 0;
  Move best = engine.GetBestMove(score);

  const SearchTrace& trace = engine.GetSearchTrace();
  S8 root_player = board.GetPlayerToMove();
  string best_uci = SearchTrace::MoveToUci(best, root_player);

  cout << "{" << endl;
  cout << "  \"fen\": \"" << EscapeJson(fen) << "\"," << endl;
  cout << "  \"best_move\": \"" << EscapeJson(best_uci) << "\"," << endl;
  cout << "  \"score\": " << score << "," << endl;
  cout << "  \"depth\": " << trace.final_depth << "," << endl;
  cout << "  \"tree\": ";
  PrintTraceNode(trace.root, 2);
  cout << endl;
  cout << "}" << endl;

  return 0;
}
