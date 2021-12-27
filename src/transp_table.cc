/* Noah Himed
 *
 * Implement the TranspTable type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "transp_table.h"

#include "board.h"

namespace omegazero {

TranspTable::TranspTable() {
  always_replace_entries_ = new TableEntry[kTableSize];
  depth_pref_entries_ = new TableEntry[kTableSize];
  // Store which slots are occupied.
  occ_table_ = new bool[kTableSize];
  // Initialize all slots intable_entry the occupancy table to unoccupied.
  Clear();
}

TranspTable::~TranspTable() {
  delete always_replace_entries_;
  delete depth_pref_entries_;
  delete occ_table_;
}

auto TranspTable::Access(const Board* board, int depth, int& eval) const
    -> bool {
  U64 board_hash = board->GetBoardHash();
  int index = board_hash & kHashMask;
  if (occ_table_[index]) {
    TableEntry table_entry = depth_pref_entries_[index];
    // Check that the current node is to be searched at a lower depth than the
    // stored evaluation was assessed for.
    if (depth <= table_entry.depth && table_entry.board_hash == board_hash) {
      eval = table_entry.eval;
      return true;
    }
    // Check the "always replace" stored evaluation if the stored "depth
    // preferred" evaluation can't be used.
    table_entry = always_replace_entries_[index];
    if (depth <= table_entry.depth && table_entry.board_hash == board_hash) {
      eval = table_entry.eval;
      return true;
    }
  }
  return false;
}

auto TranspTable::Update(const Board* board, int depth, int eval) -> void {
  U64 board_hash = board->GetBoardHash();
  TableEntry new_entry;
  new_entry.depth = depth;
  new_entry.eval = eval;
  new_entry.board_hash = board_hash;
  int index = board_hash & kHashMask;
  if (occ_table_[index]) {
    TableEntry depth_pref_entry = depth_pref_entries_[index];
    if (depth > depth_pref_entry.depth) {
      // Overwrite the depth preferred entry if the new position is evaluated
      // with deeper depth than the depth of the depth preferred entry.
      depth_pref_entries_[index] = new_entry;
    } else {
      always_replace_entries_[index] = new_entry;
    }
  } else {
    always_replace_entries_[index] = new_entry;
    depth_pref_entries_[index] = new_entry;
    occ_table_[index] = true;
  }
}

}  // namespace omegazero