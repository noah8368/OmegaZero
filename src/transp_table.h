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

namespace omegazero {

constexpr int kTableSize = 1 << 20;
// Store a mask with least significant 19 bits set for computing table indices.
constexpr U64 kHashMask = 0X7FFFF;

struct TableEntry {
  int depth;
  int eval;
  U64 board_hash;
};

class TranspTable {
 public:
  TranspTable();

  ~TranspTable();

  // Loop up the board position in the hash table and set eval to the
  // corresponding evaluation if the position is found. Return a bool to
  // indicate if the position was found.
  auto Access(const Board* board, int depth, int& eval) const -> bool;

  auto Update(const Board* board, int depth, int eval) -> void;
  auto Clear() -> void;

 private:
  // Store which slots in the table are occupied.
  bool* occ_table_;

  TableEntry* always_replace_entries_;
  TableEntry* depth_pref_entries_;
};

inline auto TranspTable::Clear() -> void {
  memset(occ_table_, false, kTableSize * sizeof(bool));
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_TRANSP_TABLE_H