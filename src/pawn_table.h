/* Noah Himed
 *
 * Define and implement the PawnTable type, a Transposition Table to store
 * pawn structure evaluations.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_PAWN_TABLE_H
#define OMEGAZERO_SRC_PAWN_TABLE_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace omegazero {

using std::begin;
using std::copy;
using std::end;
using std::fill;
using std::vector;

typedef uint64_t U64;

// Store a mask with least significant 19 bits set for computing table indices.
constexpr int kPawnTableSize = 1 << 20;
constexpr U64 kPawnHashMask = 0X7FFFF;

class PawnTable {
 public:
  PawnTable();

  // Loop up the board position in the hash table and set eval to the
  // corresponding evaluation if the position is found. Return a bool to
  // indicate if the position was found.
  auto Access(U64 pawn_hash, int& pawn_eval) const -> bool;

  auto Update(U64 pawn_hash, int pawn_eval) -> void;
  auto Clear() -> void;

 private:
  // Store which slots in the table are occupied.
  vector<bool> occupancy_table_;

  struct TableEntry {
    U64 pawn_hash;
    int pawn_eval;
  };

  vector<TableEntry> entries_;
};

inline PawnTable::PawnTable() {
  entries_.reserve(kPawnTableSize);
  occupancy_table_.reserve(kPawnTableSize);
  // Initialize all slots intable_entry the occupancy table to unoccupied.
  Clear();
}

inline auto PawnTable::Access(U64 pawn_hash, int& pawn_eval) const -> bool {
  int index = pawn_hash & kPawnHashMask;
  if (occupancy_table_[index]) {
    TableEntry entry = entries_[index];
    if (entry.pawn_hash == pawn_hash) {
      pawn_eval = entry.pawn_eval;
      return true;
    }
  }
  return false;
}

inline auto PawnTable::Update(U64 pawn_hash, int pawn_eval) -> void {
  TableEntry entry;
  entry.pawn_eval = pawn_eval;
  entry.pawn_hash = pawn_hash;
  int index = pawn_hash & kPawnHashMask;
  entries_[index] = entry;
  occupancy_table_[index] = true;
}

inline auto PawnTable::Clear() -> void {
  fill(occupancy_table_.begin(), occupancy_table_.end(), false);
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_PAWN_TABLE_H
