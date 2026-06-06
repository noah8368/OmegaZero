#ifndef OMEGAZERO_SRC_SEARCH_TRACE_H_
#define OMEGAZERO_SRC_SEARCH_TRACE_H_

#ifdef SEARCH_TRACE

#include <string>
#include <vector>

#include "board.h"
#include "move.h"

namespace omegazero {

struct TraceNode {
  std::string move_uci;
  int eval = 0;
  bool pruned = false;
  std::string prune_reason;
  bool is_pv = false;
  std::vector<TraceNode> children;
};

struct SearchTrace {
  int max_trace_ply = 3;
  int final_depth = 0;
  bool recording = false;
  TraceNode root;

  auto Clear() -> void {
    root = {};
    root.move_uci = "root";
  }

  static auto MoveToUci(const Move& move, S8 player) -> std::string {
    std::string s;
    if (move.castling_type == kNA) {
      s += static_cast<char>('a' + GetFileFromSq(move.start_sq));
      s += static_cast<char>('1' + GetRankFromSq(move.start_sq));
      s += static_cast<char>('a' + GetFileFromSq(move.target_sq));
      s += static_cast<char>('1' + GetRankFromSq(move.target_sq));
      if (move.promoted_to_piece != kNA) {
        const char promo[] = {0, 'n', 'b', 'r', 'q', 0};
        s += promo[move.promoted_to_piece];
      }
    } else {
      if (move.castling_type == kQueenSide) {
        s = (player == kWhite) ? "e1c1" : "e8c8";
      } else {
        s = (player == kWhite) ? "e1g1" : "e8g8";
      }
    }
    return s;
  }
};

}  // namespace omegazero

#endif  // SEARCH_TRACE
#endif  // OMEGAZERO_SRC_SEARCH_TRACE_H_
