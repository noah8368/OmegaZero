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
#include <vector>

#include "board.h"
#include "move.h"

namespace omegazero {

using std::begin;
using std::end;
using std::fill;
using std::vector;

enum NodeType : S8 {
  kPvNode,
  kCutNode,
  kAllNode,
};

constexpr int kTableSize = 1 << 20;

// Packed to 24 bytes (from 32): U64 leads for 8-byte alignment, then the
// 8-byte Move, then the narrow fields. `eval` fits int16_t (evals are bounded
// by +/-kBestEval, +/-32128 after mate-rebasing in ScoreToTt). `search_depth`
// stays int16_t rather than S8 because a root depth can reach kSearchLimit
// (128), which overflows a signed 8-bit field. Two vectors of kTableSize
// entries makes this ~16 MB saved plus tighter probe cache behavior.
struct TableEntry {
  U64 board_hash;
  Move hash_move;
  S16 eval;
  S16 search_depth;
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

  auto GetHashEntry(const Board* board) const -> TableEntry;
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
  always_replace_entries_.resize(kTableSize);
  depth_pref_entries_.resize(kTableSize);
  occupancy_table_.resize(kTableSize, false);
}

inline auto TranspositionTable::GetHashMove(const Board* board) const -> Move {
  TableEntry entry = GetHashEntry(board);
  return entry.hash_move;
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
