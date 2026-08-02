/* Noah Himed
 *
 * Define the SearchThread and SearchPool types to manage multi-threaded search
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

struct SearchThread {
  Board board;
  Engine engine;
};

class SearchPool {
 public:
  auto Search(const Board& root, const vector<U64>& pos_hist) -> Move;

 private:
  TranspositionTable tt_;
  std::vector<SearchThread> threads_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_SEARCH_POOL_H