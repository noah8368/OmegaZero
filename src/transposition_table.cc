/* Noah Himed
 *
 * Implement the TranspositionTable type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "transposition_table.h"

#include "board.h"
#include "move.h"

namespace omegazero {

constexpr U64 kHashMask = 0X7FFFF;

auto TranspositionTable::Access(const Board* board, int depth, int& eval,
                                S8& node_type) const -> bool {
  U64 board_hash = board->GetBoardHash();
  int index = board_hash & kHashMask;
  if (occupancy_table_[index]) {
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

auto TranspositionTable::PosIsPvNode(const Board* board) const -> bool {
  U64 board_hash = board->GetBoardHash();
  int index = board_hash & kHashMask;
  if (occupancy_table_[index]) {
    TableEntry table_entry = depth_pref_entries_[index];

    // Check the "depth preferred" table first.
    if (table_entry.board_hash == board_hash) {
      return table_entry.node_type == kPvNode;
    }

    // Check the "always replace" table if a collision was detected in the
    // "depth preferred" table.
    table_entry = always_replace_entries_[index];
    if (table_entry.board_hash == board_hash) {
      return table_entry.node_type == kPvNode;
    }
  }
  return false;
}

auto TranspositionTable::GetHashMove(const Board* board) const -> Move {
  U64 board_hash = board->GetBoardHash();
  int index = board_hash & kHashMask;
  Move hash_move;
  if (occupancy_table_[index]) {
    TableEntry table_entry = depth_pref_entries_[index];
    // Check the "depth preferred" table first.
    if (table_entry.board_hash == board_hash) {
      hash_move = table_entry.hash_move;
    } else {
      // Check the "always replace" table if a collision was detected in the
      // "depth preferred" table.
      table_entry = always_replace_entries_[index];
      if (table_entry.board_hash == board_hash) {
        hash_move = table_entry.hash_move;
      }
    }
  }
  return hash_move;
}

auto TranspositionTable::Update(const Board* board, int depth, int eval,
                                S8 node_type, const Move& hash_move) -> void {
  TableEntry new_entry;
  new_entry.hash_move = hash_move;
  U64 board_hash = board->GetBoardHash();
  new_entry.board_hash = board_hash;
  new_entry.search_depth = depth;
  new_entry.eval = eval;
  new_entry.node_type = node_type;

  int index = board_hash & kHashMask;
  if (occupancy_table_[index]) {
    if (new_entry.search_depth > depth_pref_entries_[index].search_depth) {
      // Overwrite the depth preferred entry if the new position is evaluated
      // with deeper depth than the depth of the depth preferred entry.
      depth_pref_entries_[index] = new_entry;
    } else {
      always_replace_entries_[index] = new_entry;
    }
  } else {
    always_replace_entries_[index] = new_entry;
    depth_pref_entries_[index] = new_entry;
    occupancy_table_[index] = true;
  }
}

}  // namespace omegazero
