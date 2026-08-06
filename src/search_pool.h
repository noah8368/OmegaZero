/* Noah Himed
 *
 * Define the SearchContext and SearchPool types to manage multi-threaded search
 * using the Lazy SMP method.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_SEARCH_POOL_H
#define OMEGAZERO_SRC_SEARCH_POOL_H

#include <memory>
#include <thread>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "transposition_table.h"

namespace omegazero {

struct SearchContext {
  SearchContext(TranspositionTable* tt, const Board& board,
                const vector<U64>& pos_hist, float search_time);
  Board board_;
  Engine engine_;
};

class SearchPool {
 public:
  SearchPool(S8 num_threads);
  auto LazySmpSearch(const Board& board, const vector<U64>& pos_history,
                     float search_time) -> Move;

 private:
  // Stops and joins the helper threads on scope exit, so an exception thrown
  // between spawning them and the explicit teardown can't destroy joinable
  // threads (which calls std::terminate). Helpers run SetInfiniteSearch(), so
  // they must be told to stop before join() or it would block forever.
  class HelperTeardown {
   public:
    HelperTeardown(vector<std::unique_ptr<SearchContext>>& ctxs,
                   vector<std::thread>& threads)
        : ctxs_(ctxs), threads_(threads) {}
    ~HelperTeardown() {
      for (auto& ctx : ctxs_) {
        ctx->engine_.RequestStop();
      }
      for (auto& thread : threads_) {
        if (thread.joinable()) {
          thread.join();
        }
      }
    }
    HelperTeardown(const HelperTeardown&) = delete;
    auto operator=(const HelperTeardown&) -> HelperTeardown& = delete;

   private:
    vector<std::unique_ptr<SearchContext>>& ctxs_;
    vector<std::thread>& threads_;
  };

  S8 num_helpers_;
  TranspositionTable tt_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_SEARCH_POOL_H