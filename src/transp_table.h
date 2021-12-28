/* Noah Himed
 *
 * Define and implement the TranspTable type, a custom implementation of a
 * Transposition Table.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_TRANSP_TABLE_H
#define OMEGAZERO_SRC_TRANSP_TABLE_H

#include <cstring>
#include <stdexcept>

#include "board.h"
#include "move.h"

namespace omegazero {

enum NodeType : S8 {
  kPvNode,
  kCutNode,
  kAllNode,
};

constexpr int kTableSize = 1 << 20;
// Store a mask with least significant 19 bits set for computing table indices.
constexpr U64 kHashMask = 0X7FFFF;

struct TableEntry {
  Move best_move;
  U64 board_hash;
  int depth;
  int eval;
};

class TranspTable {
 public:
  TranspTable();

  ~TranspTable();

  // Loop up the board position in the hash table and set eval to the
  // corresponding evaluation if the position is found. Return a bool to
  // indicate if the position was found.
  auto Access(const Board* board, int depth, int& eval, Move& best_move) const
      -> bool;
  auto Access(const Board* board, int depth, int& eval) const -> bool;
  auto Access(const Board* board, int depth, Move& best_move) const -> bool;

  auto Update(const Board* board, int depth, int eval,
              const Move* best_move = nullptr) -> void;
  auto Clear() -> void;

 private:
  // Store which slots in the table are occupied.
  bool* occ_table_;

  TableEntry* always_replace_entries_;
  TableEntry* depth_pref_entries_;
};

inline auto TranspTable::Access(const Board* board, int depth, int& eval) const
    -> bool {
  Move throwaway_move;
  return Access(board, depth, eval, throwaway_move);
}

inline auto TranspTable::Access(const Board* board, int depth,
                                Move& best_move) const -> bool {
  int throwaway_eval;
  return Access(board, depth, throwaway_eval, best_move);
}

inline auto TranspTable::Clear() -> void {
  memset(occ_table_, false, kTableSize * sizeof(bool));
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_TRANSP_TABLE_H