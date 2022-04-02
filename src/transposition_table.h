/* Noah Himed
 *
 * Define and implement the TranspositionTable type, a custom implementation of
 * a Transposition Table.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_TRANSPOSITION_TABLE_H
#define OMEGAZERO_SRC_TRANSPOSITION_TABLE_H

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "board.h"
#include "move.h"

namespace omegazero {

using std::begin;
using std::copy;
using std::end;
using std::fill;
using std::vector;

enum NodeType : S8 {
  kPvNode,
  kCutNode,
  kAllNode,
};

// Store a mask with least significant 19 bits set for computing table indices.
constexpr int kTableSize = 1 << 20;

struct TableEntry {
  Move hash_move;
  U64 board_hash;
  int eval;
  int search_depth;
  S8 node_type;
};

class TranspositionTable {
 public:
  TranspositionTable();

  // Loop up the board position in the hash table and set eval to the
  // corresponding evaluation if the position is found. Return a bool to
  // indicate if the position was found.
  auto Access(const Board* board, int depth, int& eval, S8& node_type) const
      -> bool;
  // Return if the given board position has been stored as a PV node.
  auto PosIsPvNode(const Board* board) const -> bool;

  auto GetHashMove(const Board* board) const -> Move;

  auto Update(const Board* board, int depth, int eval, S8 node_type,
              const Move& hash_move) -> void;
  auto Update(const Board* board, int depth, int eval, S8 node_type) -> void;
  auto Clear() -> void;

 private:
  // Store which slots in the table are occupied.
  vector<bool> occupancy_table_;

  vector<TableEntry> always_replace_entries_;
  vector<TableEntry> depth_pref_entries_;
};

inline TranspositionTable::TranspositionTable() {
  always_replace_entries_.reserve(kTableSize);
  depth_pref_entries_.reserve(kTableSize);
  occupancy_table_.reserve(kTableSize);
  // Initialize all slots intable_entry the occupancy table to unoccupied.
  Clear();
}

inline auto TranspositionTable::Update(const Board* board, int depth, int eval,
                                       S8 node_type) -> void {
  Move throwaway_move;
  Update(board, depth, eval, node_type, throwaway_move);
}

inline auto TranspositionTable::Clear() -> void {
  fill(occupancy_table_.begin(), occupancy_table_.end(), false);
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_TRANSPOSITION_TABLE_H
