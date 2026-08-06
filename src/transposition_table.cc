/* Noah Himed
 *
 * Implement the TranspositionTable type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "transposition_table.h"

#include <cassert>
#include <cstdint>
#include <cstring>

#include "board.h"
#include "move.h"

namespace omegazero {
namespace {

constexpr U64 kHashMask = kTableSize - 1;

inline auto PackMove(const Move& move) -> U64 {
  U64 word = 0;
  std::memcpy(&word, &move, sizeof(Move));
  return word;
}
inline auto UnpackMove(U64 word) -> Move {
  Move move;
  std::memcpy(&move, &word, sizeof(Move));
  return move;
}
// data_info layout: bits [0,16) eval, [16,32) search_depth, [32,34) node_type.
inline auto PackInfo(int eval, int depth, S8 node_type) -> U64 {
  return static_cast<U64>(static_cast<uint16_t>(eval)) |
         (static_cast<U64>(static_cast<uint16_t>(depth)) << 16) |
         (static_cast<U64>(static_cast<uint8_t>(node_type)) << 32);
}
inline auto InfoEval(U64 info) -> S16 {
  return static_cast<S16>(info & 0xFFFF);
}
inline auto InfoDepth(U64 info) -> S16 {
  return static_cast<S16>((info >> 16) & 0xFFFF);
}
inline auto InfoNodeType(U64 info) -> S8 {
  return static_cast<S8>((info >> 32) & 0x3);
}

}  // namespace

auto TranspositionTable::ProbeSlot(const TtSlot& slot, U64 board_hash,
                                   TableEntry& out) -> bool {
  U64 key = slot.key.load(std::memory_order_relaxed);
  U64 data_move = slot.data_move.load(std::memory_order_relaxed);
  U64 data_info = slot.data_info.load(std::memory_order_relaxed);
  // A torn read (words from different writes) recomputes the wrong hash here and
  // is rejected as a miss.
  if ((key ^ data_move ^ data_info) != board_hash) {
    return false;
  }
  out.board_hash = board_hash;
  out.hash_move = UnpackMove(data_move);
  out.eval = InfoEval(data_info);
  out.search_depth = InfoDepth(data_info);
  out.node_type = InfoNodeType(data_info);
  return true;
}

auto TranspositionTable::StoreSlot(TtSlot& slot, U64 board_hash,
                                   const Move& hash_move, int eval, int depth,
                                   S8 node_type) -> void {
  U64 data_move = PackMove(hash_move);
  U64 data_info = PackInfo(eval, depth, node_type);
  slot.key.store(board_hash ^ data_move ^ data_info,
                 std::memory_order_relaxed);
  slot.data_move.store(data_move, std::memory_order_relaxed);
  slot.data_info.store(data_info, std::memory_order_relaxed);
}

auto TranspositionTable::ProbeEval(const Board* board, int depth, int& eval,
                                   S8& node_type) const -> bool {
  assert(board != nullptr);
  U64 board_hash = board->GetBoardHash();
  U64 index = board_hash & kHashMask;
  TableEntry entry{};
  // Depth-preferred tier first, then always-replace; a hit is usable only if it
  // was searched at least as deep as the current node needs.
  if (ProbeSlot(depth_pref_[index], board_hash, entry) &&
      depth <= entry.search_depth) {
    eval = entry.eval;
    node_type = entry.node_type;
    return true;
  }
  if (ProbeSlot(always_replace_[index], board_hash, entry) &&
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
  U64 index = board_hash & kHashMask;
  TableEntry entry{};
  if (ProbeSlot(depth_pref_[index], board_hash, entry)) {
    return entry.node_type == kPvNode;
  }
  if (ProbeSlot(always_replace_[index], board_hash, entry)) {
    return entry.node_type == kPvNode;
  }
  return false;
}

auto TranspositionTable::GetHashEntry(const Board* board) const -> TableEntry {
  assert(board != nullptr);
  U64 board_hash = board->GetBoardHash();
  U64 index = board_hash & kHashMask;
  TableEntry entry{};
  if (ProbeSlot(depth_pref_[index], board_hash, entry)) {
    return entry;
  }
  if (ProbeSlot(always_replace_[index], board_hash, entry)) {
    return entry;
  }
  return entry;  // miss: empty entry (hash_move is empty, all fields zero)
}

auto TranspositionTable::Update(const Board* board, int depth, int eval,
                                S8 node_type, const Move& hash_move) -> void {
  assert(board != nullptr);
  U64 board_hash = board->GetBoardHash();
  U64 index = board_hash & kHashMask;

  // First write since construction/Clear seeds both tiers, preserving the old
  // occupancy semantics (an untouched index has both slots all-zero). A real
  // stored entry effectively never has key 0, so this only fires once per index.
  U64 dp_key = depth_pref_[index].key.load(std::memory_order_relaxed);
  U64 ar_key = always_replace_[index].key.load(std::memory_order_relaxed);
  if (dp_key == 0 && ar_key == 0) {
    StoreSlot(depth_pref_[index], board_hash, hash_move, eval, depth, node_type);
    StoreSlot(always_replace_[index], board_hash, hash_move, eval, depth,
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
    StoreSlot(depth_pref_[index], board_hash, hash_move, eval, depth, node_type);
  } else {
    StoreSlot(always_replace_[index], board_hash, hash_move, eval, depth,
              node_type);
  }
}

auto TranspositionTable::Clear() -> void {
  for (int i = 0; i < kTableSize; ++i) {
    always_replace_[i].key.store(0, std::memory_order_relaxed);
    always_replace_[i].data_move.store(0, std::memory_order_relaxed);
    always_replace_[i].data_info.store(0, std::memory_order_relaxed);
    depth_pref_[i].key.store(0, std::memory_order_relaxed);
    depth_pref_[i].data_move.store(0, std::memory_order_relaxed);
    depth_pref_[i].data_info.store(0, std::memory_order_relaxed);
  }
}

}  // namespace omegazero
