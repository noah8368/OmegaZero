/* Noah Himed
 *
 * Implement multi-threaded search with the SearchPool type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "search_pool.h"

#include "engine.h"

namespace omegazero {

SearchContext::SearchContext(const Board& board,
                             const std::vector<U64>& pos_history,
                             float search_time)
    : board_(board),
      engine_(&board_, board_.GetPlayerToMove(), search_time, pos_history) {}

SearchPool::SearchPool(S8 num_threads) { num_threads_ = num_threads; }

auto SearchPool::LazySmpSearch(const Board& board,
                               const vector<U64>& pos_history,
                               float search_time) -> Move {
  for (S8 thread_idx = 0; thread_idx < num_threads_; ++thread_idx) {
    SearchContext search_context(board, pos_history, search_time);
  }
  return Move{};
}

}  // namespace omegazero
