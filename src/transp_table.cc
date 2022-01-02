/* Noah Himed
 *
 * Implement the TranspTable type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "transp_table.h"

#include "board.h"
#include "move.h"

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

auto TranspTable::Access(const Board* board, int depth, int& eval,
                         S8& node_type) const -> bool {
  U64 board_hash = board->GetBoardHash();
  int index = board_hash & kHashMask;
  if (occ_table_[index]) {
    TableEntry table_entry = depth_pref_entries_[index];
    // Check that the current node is to be searched at a lower depth than the
    // stored evaluation was assessed for.
    if (depth <= table_entry.search_depth &&
        table_entry.board_hash == board_hash) {
      eval = table_entry.eval;
      node_type = table_entry.node_type;
      return true;
    }
    // Check the "always replace" stored evaluation if the stored "depth
    // preferred" evaluation can't be used.
    table_entry = always_replace_entries_[index];
    if (depth <= table_entry.search_depth &&
        table_entry.board_hash == board_hash) {
      eval = table_entry.eval;
      node_type = table_entry.node_type;
      return true;
    }
  }
  return false;
}

auto TranspTable::GetPvMove(const Board* board) const -> Move {
  U64 board_hash = board->GetBoardHash();
  int index = board_hash & kHashMask;
  Move pv_move;
  if (occ_table_[index]) {
    TableEntry table_entry = depth_pref_entries_[index];
    // Check that the current node is to be searched at a lower depth than the
    // stored evaluation was assessed for.
    if (table_entry.node_type == kPvNode &&
        table_entry.board_hash == board_hash) {
      pv_move = table_entry.pv_move;
    } else {
      // Check the "always replace" stored evaluation if the stored "depth
      // preferred" evaluation can't be used.
      table_entry = always_replace_entries_[index];
      if (table_entry.node_type == kPvNode &&
          table_entry.board_hash == board_hash) {
        pv_move = table_entry.pv_move;
      }
    }
  }
  return pv_move;
}

auto TranspTable::Update(const Board* board, int depth, int eval, S8 node_type,
                         const Move* pv_move) -> void {
  TableEntry new_entry;
  if (pv_move) {
    new_entry.pv_move = *pv_move;
  }
  U64 board_hash = board->GetBoardHash();
  new_entry.board_hash = board_hash;
  new_entry.search_depth = depth;
  new_entry.eval = eval;
  new_entry.node_type = node_type;

  int index = board_hash & kHashMask;
  if (occ_table_[index]) {
    if (new_entry.search_depth < depth_pref_entries_[index].search_depth) {
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