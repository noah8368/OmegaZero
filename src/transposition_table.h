/* Noah Himed
 *
 * Define and implement the TranspositionTable type, a custom implementation of
 * a Transposition Table.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_TRANSPOSITION_TABLE_H
#define OMEGAZERO_SRC_TRANSPOSITION_TABLE_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

#include "board.h"
#include "move.h"

namespace omegazero {

typedef uint8_t U8;
typedef uint16_t U16;

enum NodeType : S8 {
  kPvNode,
  kCutNode,
  kAllNode,
};

// Total memory budget for the transposition table, in MiB (both tiers). The
// per-tier slot count is derived from this and sizeof(TtSlot); see kNumSlots.
constexpr int kTtSizeMb = 64;

// Largest power of two <= n. The slot count must be a power of two so that
// board_hash & (count - 1) can index the table without a modulo.
constexpr auto Pow2Floor(U64 n) -> U64 {
  U64 pow = 1;
  while (pow <= n / 2) {
    pow *= 2;
  }
  return pow;
}

// Decoded view of a stored entry, handed back to callers on a probe. The table
// stores each entry as a lockless three-word TtSlot (below); this is just the
// unpacked form.
struct TableEntry {
  Move hash_move;
  S16 eval;
  S16 search_depth;
  S8 node_type;
};

// Lockless slot (Hyatt's XOR trick) enabling concurrent Lazy-SMP access with no
// locks. An entry spans three 64-bit words; an aligned 64-bit atomic load/store
// never tears, and `key` stores board_hash ^ data_move ^ data_info so a reader
// that catches a half-completed write (one word from a new entry, another from
// the old) recomputes a mismatching hash and treats the slot as a miss.
// `relaxed` ordering suffices: every slot self-validates, and the search never
// uses the TT to order other memory. An all-zero slot is "empty" (0 matches only
// hash 0, which a real Zobrist key never is), so no occupancy table is needed.
struct TtSlot {
  std::atomic<U64> key{0};
  std::atomic<U64> data_move{0};
  std::atomic<U64> data_info{0};
};
static_assert(std::atomic<U64>::is_always_lock_free,
              "TT lockless scheme requires lock-free 64-bit atomics");
static_assert(sizeof(Move) == 8 && std::is_trivially_copyable<Move>::value,
              "TT packs Move into one 64-bit word via memcpy");

// Pack/unpack the two payload words. `data_move` holds the 8-byte Move;
// `data_info` packs eval into bits [0,16), search_depth into [16,32), and
// node_type into [32,34).
auto PackMove(const Move& move) -> U64;
auto UnpackMove(U64 word) -> Move;
auto PackInfo(int eval, int depth, S8 node_type) -> U64;
auto InfoEval(U64 info) -> S16;
auto InfoDepth(U64 info) -> S16;
auto InfoNodeType(U64 info) -> S8;
// Whole-entry helpers. UnpackSlot decodes `slot` into `out` iff it holds a
// consistent entry for `board_hash` (a torn or non-matching slot is a miss);
// PackSlot encodes an entry into `slot` (three relaxed word stores).
auto UnpackSlot(const TtSlot& slot, U64 board_hash, TableEntry& out) -> bool;
auto PackSlot(TtSlot& slot, U64 board_hash, const Move& hash_move, int eval,
              int depth, S8 node_type) -> void;

class TranspositionTable {
 public:
  TranspositionTable();

  // Look up the board position; on a hit searched at least as deep as `depth`,
  // set `eval`/`node_type` and return true, else return false.
  auto ProbeEval(const Board* board, int depth, int& eval, S8& node_type) const
      -> bool;
  // Return whether the position is stored as a PV node.
  auto PosIsPvNode(const Board* board) const -> bool;

  auto GetHashEntry(const Board* board) const -> TableEntry;
  auto GetHashMove(const Board* board) const -> Move;

  auto Update(const Board* board, int depth, int eval, S8 node_type,
              const Move& hash_move) -> void;
  auto Update(const Board* board, int depth, int eval, S8 node_type) -> void;
  auto Clear() -> void;

 private:
  // Per-tier slot count: the kTtSizeMb budget split across the two tiers and
  // sizeof(TtSlot), floored to a power of two for masked indexing.
  static constexpr U64 kNumSlots = Pow2Floor(
      static_cast<U64>(kTtSizeMb) * 1024 * 1024 / (2 * sizeof(TtSlot)));
  static constexpr U64 kIndexMask = kNumSlots - 1;

  std::unique_ptr<TtSlot[]> always_replace_;
  std::unique_ptr<TtSlot[]> depth_pref_;
};

// --- Inline, non-member functions ---

inline auto PackMove(const Move& move) -> U64 {
  U64 word = 0;
  // Move has default member initializers, so it is non-trivial (though still
  // trivially copyable); cast to void* so GCC's -Wclass-memaccess accepts the
  // byte copy. Safe: see the static_assert that Move fits in one 64-bit word.
  std::memcpy(&word, static_cast<const void*>(&move), sizeof(Move));
  return word;
}

inline auto UnpackMove(U64 word) -> Move {
  Move move;
  std::memcpy(static_cast<void*>(&move), &word, sizeof(Move));
  return move;
}

inline auto PackInfo(int eval, int depth, S8 node_type) -> U64 {
  return static_cast<U64>(static_cast<U16>(eval)) |
         (static_cast<U64>(static_cast<U16>(depth)) << 16) |
         (static_cast<U64>(static_cast<U8>(node_type)) << 32);
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

inline auto UnpackSlot(const TtSlot& slot, U64 board_hash, TableEntry& out)
    -> bool {
  U64 key = slot.key.load(std::memory_order_relaxed);
  U64 data_move = slot.data_move.load(std::memory_order_relaxed);
  U64 data_info = slot.data_info.load(std::memory_order_relaxed);
  // A torn read (words from different writes) recomputes the wrong hash here and
  // is rejected as a miss.
  if ((key ^ data_move ^ data_info) != board_hash) {
    return false;
  }
  out.hash_move = UnpackMove(data_move);
  out.eval = InfoEval(data_info);
  out.search_depth = InfoDepth(data_info);
  out.node_type = InfoNodeType(data_info);
  return true;
}

inline auto PackSlot(TtSlot& slot, U64 board_hash, const Move& hash_move,
                     int eval, int depth, S8 node_type) -> void {
  U64 data_move = PackMove(hash_move);
  U64 data_info = PackInfo(eval, depth, node_type);
  slot.key.store(board_hash ^ data_move ^ data_info, std::memory_order_relaxed);
  slot.data_move.store(data_move, std::memory_order_relaxed);
  slot.data_info.store(data_info, std::memory_order_relaxed);
}

// --- Inline member functions ---

inline TranspositionTable::TranspositionTable()
    : always_replace_(new TtSlot[kNumSlots]),
      depth_pref_(new TtSlot[kNumSlots]) {}

inline auto TranspositionTable::GetHashMove(const Board* board) const -> Move {
  return GetHashEntry(board).hash_move;
}

inline auto TranspositionTable::Update(const Board* board, int depth, int eval,
                                       S8 node_type) -> void {
  Update(board, depth, eval, node_type, Move{});
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_TRANSPOSITION_TABLE_H
