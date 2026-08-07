/* Noah Himed
 *
 * Implement the TranspositionTable type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "transposition_table.h"

#include <cassert>

#include "board.h"
#include "move.h"

namespace omegazero {

auto TranspositionTable::ProbeEval(const Board* board, int depth, int& eval,
                                   S8& node_type) const -> bool {
  assert(board != nullptr);
  U64 board_hash = board->GetBoardHash();
  U64 index = board_hash & kIndexMask;
  TableEntry entry{};
  // Depth-preferred tier first, then always-replace; a hit is usable only if it
  // was searched at least as deep as the current node needs.
  if (UnpackSlot(depth_pref_[index], board_hash, entry) &&
      depth <= entry.search_depth) {
    eval = entry.eval;
    node_type = entry.node_type;
    return true;
  }
  if (UnpackSlot(always_replace_[index], board_hash, entry) &&
      depth <= entry.search_depth) {
    eval = entry.eval;
    node_type = entry.node_type;
    return true;
  }
  return false;
}

auto TranspositionTable::PosIsPvNode(const Board* board) const -> bool {
  assert(board != nullptr);
  U64 board_hash = board->GetBoardHash();
  U64 index = board_hash & kIndexMask;
  TableEntry entry{};
  if (UnpackSlot(depth_pref_[index], board_hash, entry)) {
    return entry.node_type == kPvNode;
  }
  if (UnpackSlot(always_replace_[index], board_hash, entry)) {
    return entry.node_type == kPvNode;
  }
  return false;
}

auto TranspositionTable::GetHashEntry(const Board* board) const -> TableEntry {
  assert(board != nullptr);
  U64 board_hash = board->GetBoardHash();
  U64 index = board_hash & kIndexMask;
  TableEntry entry{};
  if (UnpackSlot(depth_pref_[index], board_hash, entry)) {
    return entry;
  }
  if (UnpackSlot(always_replace_[index], board_hash, entry)) {
    return entry;
  }
  return entry;  // miss: empty entry (hash_move is empty, all fields zero)
}

auto TranspositionTable::Update(const Board* board, int depth, int eval,
                                S8 node_type, const Move& hash_move) -> void {
  assert(board != nullptr);
  U64 board_hash = board->GetBoardHash();
  U64 index = board_hash & kIndexMask;

  // First write since construction/Clear seeds both tiers, preserving the old
  // occupancy semantics (an untouched index has both slots all-zero). A real
  // stored entry effectively never has key 0, so this only fires once per
  // index.
  U64 dp_key = depth_pref_[index].key.load(std::memory_order_relaxed);
  U64 ar_key = always_replace_[index].key.load(std::memory_order_relaxed);
  if (dp_key == 0 && ar_key == 0) {
    PackSlot(depth_pref_[index], board_hash, hash_move, eval, depth,
             node_type);
    PackSlot(always_replace_[index], board_hash, hash_move, eval, depth,
             node_type);
    return;
  }

  // Depth-preferred replacement: take the depth-preferred slot when this entry
  // is deeper than the current occupant (its raw stored depth needs no XOR
  // validation -- we only care how deep whatever resides there is), otherwise
  // fall back to the always-replace slot.
  U64 resident_info =
      depth_pref_[index].data_info.load(std::memory_order_relaxed);
  if (depth > InfoDepth(resident_info)) {
    PackSlot(depth_pref_[index], board_hash, hash_move, eval, depth,
             node_type);
  } else {
    PackSlot(always_replace_[index], board_hash, hash_move, eval, depth,
             node_type);
  }
}

auto TranspositionTable::Clear() -> void {
  for (U64 entry_idx = 0; entry_idx < kNumSlots; ++entry_idx) {
    always_replace_[entry_idx].key.store(0, std::memory_order_relaxed);
    always_replace_[entry_idx].data_move.store(0, std::memory_order_relaxed);
    always_replace_[entry_idx].data_info.store(0, std::memory_order_relaxed);
    depth_pref_[entry_idx].key.store(0, std::memory_order_relaxed);
    depth_pref_[entry_idx].data_move.store(0, std::memory_order_relaxed);
    depth_pref_[entry_idx].data_info.store(0, std::memory_order_relaxed);
  }
}

}  // namespace omegazero
