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
#include <memory>
#include <type_traits>

#include "board.h"
#include "move.h"

namespace omegazero {

enum NodeType : S8 {
  kPvNode,
  kCutNode,
  kAllNode,
};

constexpr int kTableSize = 1 << 20;

// Decoded view of a stored entry, handed back to callers on a probe. Internally
// the table keeps each entry as a lockless three-word TtSlot (see the private
// section); this struct is just the unpacked form.
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
  // Lockless slot (Hyatt's XOR trick) enabling concurrent Lazy-SMP access with
  // no locks. An entry spans three 64-bit words; an aligned 64-bit atomic
  // load/store never tears, and `key` stores board_hash ^ data_move ^ data_info
  // so a reader that catches a half-completed write (one word from a new entry,
  // another from the old) recomputes a mismatching hash and treats the slot as
  // a miss. `relaxed` ordering suffices: every slot self-validates, and the
  // search never uses the TT to order other memory. An all-zero slot is
  // "empty" (0 matches only hash 0, which a real Zobrist key never is), so no
  // separate occupancy table is needed.
  struct TtSlot {
    std::atomic<U64> key{0};
    std::atomic<U64> data_move{0};
    std::atomic<U64> data_info{0};
  };
  static_assert(std::atomic<U64>::is_always_lock_free,
                "TT lockless scheme requires lock-free 64-bit atomics");
  static_assert(sizeof(Move) == 8 && std::is_trivially_copyable<Move>::value,
                "TT packs Move into one 64-bit word via memcpy");

  // Decode `slot` into `out` iff it holds a consistent entry for `board_hash`.
  static auto ProbeSlot(const TtSlot& slot, U64 board_hash, TableEntry& out)
      -> bool;
  // Encode an entry and write it into `slot` (three relaxed word stores).
  static auto StoreSlot(TtSlot& slot, U64 board_hash, const Move& hash_move,
                        int eval, int depth, S8 node_type) -> void;

  std::unique_ptr<TtSlot[]> always_replace_;
  std::unique_ptr<TtSlot[]> depth_pref_;
};

inline TranspositionTable::TranspositionTable()
    : always_replace_(new TtSlot[kTableSize]),
      depth_pref_(new TtSlot[kTableSize]) {}

inline auto TranspositionTable::GetHashMove(const Board* board) const -> Move {
  return GetHashEntry(board).hash_move;
}

inline auto TranspositionTable::Update(const Board* board, int depth, int eval,
                                       S8 node_type) -> void {
  Update(board, depth, eval, node_type, Move{});
}

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_TRANSPOSITION_TABLE_H
