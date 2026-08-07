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

// Maximum worker threads (bounds the UCI `Threads` option and --threads). Kept
// well within S8 so the main + helpers counts never overflow.
constexpr int kMaxThreads = 64;

// Default worker-thread count: the machine's hardware concurrency, clamped to
// [1, kMaxThreads]. hardware_concurrency() returns 0 when it can't determine the
// core count, in which case fall back to single-threaded.
inline auto DefaultThreadCount() -> int {
  unsigned hw = std::thread::hardware_concurrency();
  if (hw == 0) {
    return 1;
  }
  int n = static_cast<int>(hw);
  return n > kMaxThreads ? kMaxThreads : n;
}

struct SearchContext {
  SearchContext(TranspositionTable* tt, const Board& board,
                const vector<U64>& pos_hist, float search_time);
  Board board_;
  Engine engine_;
};

// Lazy SMP thread pool. Owns the shared transposition table; every worker gets
// its own Board+Engine but probes/updates the one shared (lockless) TT.
class SearchPool {
 public:
  explicit SearchPool(S8 num_threads);

  // The shared TT, injected into every worker Engine and into the caller's main
  // Engine (which it passes to LazySmpSearch).
  auto GetTt() -> TranspositionTable* { return &tt_; }

  // Set the total thread count (main + helpers); backs the UCI `Threads` option.
  auto SetNumThreads(S8 num_threads) -> void {
    num_helpers_ = num_threads > 0 ? num_threads - 1 : 0;
  }

  // Run a Lazy SMP search. `main` -- already configured by the caller and built
  // against GetTt() -- is the primary search; helper threads run unbounded on
  // copies of `root` (which is the position `main` searches), sharing the TT,
  // and are torn down when `main` returns. `pos_history` seeds every engine's
  // repetition history. Returns main's best move; a no-op wrapper at 1 thread.
  auto LazySmpSearch(Engine& main, const Board& root,
                     const vector<U64>& pos_history) -> Move;

 private:
  // Spawn / stop-and-join the helper threads. Private: callers go through
  // LazySmpSearch, which brackets the main search with these. `params` is the
  // main engine's search configuration, copied into every helper so they search
  // with the same tuned parameters (there are no in-code defaults to fall back
  // on).
  auto StartHelpers(const Board& root, const vector<U64>& pos_history,
                    const SearchParams& params) -> void;
  auto StopHelpers() -> void;

  // Stops and joins the helper threads on scope exit (RAII), so an exception in
  // the main search can't leave joinable threads (which would call
  // std::terminate). Wraps StopHelpers(), which requests stop before joining
  // since helpers run unbounded and would deadlock a bare join.
  class HelperTeardown {
   public:
    explicit HelperTeardown(SearchPool& pool) : pool_(pool) {}
    ~HelperTeardown() { pool_.StopHelpers(); }
    HelperTeardown(const HelperTeardown&) = delete;
    auto operator=(const HelperTeardown&) -> HelperTeardown& = delete;

   private:
    SearchPool& pool_;
  };

  S8 num_helpers_;
  TranspositionTable tt_;
  vector<std::unique_ptr<SearchContext>> helper_ctxs_;
  vector<std::thread> helper_threads_;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_SEARCH_POOL_H
