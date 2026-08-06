/* Noah Himed
 *
 * Define the SearchContext and SearchPool types to manage multi-threaded search
 * using the Lazy SMP method.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_SEARCH_POOL_H
#define OMEGAZERO_SRC_SEARCH_POOL_H

#include <vector>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "transposition_table.h"

namespace omegazero {

struct SearchContext {
  SearchContext(const Board& board, const vector<U64>& pos_hist,
                float search_time);
  Board board_;
  Engine engine_;
  Move found_move_;
};

class SearchPool {
 public:
  SearchPool(S8 num_threads);
  auto LazySmpSearch(const Board& board, const vector<U64>& pos_history,
                     float search_time) -> Move;

 private:
  auto SearchWorker(SearchContext& search_context) -> void;

  S8 num_threads_;
  TranspositionTable tt_;
  std::vector<SearchContext> threads_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_SEARCH_POOL_H