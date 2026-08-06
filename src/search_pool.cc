/* Noah Himed
 *
 * Implement multi-threaded search with the SearchPool type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "search_pool.h"

#include <memory>
#include <thread>

#include "engine.h"

namespace omegazero {

SearchContext::SearchContext(TranspositionTable* tt, const Board& board,
                             const std::vector<U64>& pos_history,
                             float search_time)
    : board_(board),
      // Engine's player_side is a UI-perspective char ('w'/'b'/'r') that the
      // search never reads; GetPlayerToMove() returns kWhite/kBlack (0/1), which
      // the Engine ctor would reject as an invalid side. Pass the side to move
      // as its char so construction is valid.
      engine_(tt, &board_, board_.GetPlayerToMove() == kWhite ? 'w' : 'b',
              search_time, pos_history) {}

SearchPool::SearchPool(S8 num_threads) {
  num_helpers_ = num_threads > 0 ? num_threads - 1 : 0;
}

auto SearchPool::LazySmpSearch(const Board& board,
                               const vector<U64>& pos_history,
                               float search_time) -> Move {
  // Create Engine and Board objects for each helper thread.
  vector<std::unique_ptr<SearchContext>> helper_ctxs;
  helper_ctxs.reserve(num_helpers_);
  for (S8 helper_idx = 0; helper_idx < num_helpers_; ++helper_idx) {
    helper_ctxs.push_back(
        std::make_unique<SearchContext>(&tt_, board, pos_history, search_time));
    helper_ctxs[helper_idx]->engine_.SetInfiniteSearch();
  }

  // Spin up the search helper threads with infinite search enabled. `teardown`
  // is declared before the spawn loop so it stops and joins whatever threads
  // exist on scope exit -- on a normal return, and on any exception between here
  // and the return (a mid-spawn throw, main_context construction, or
  // GetBestMove), which would otherwise leave joinable threads and terminate.
  vector<std::thread> helpers;
  helpers.reserve(num_helpers_);
  HelperTeardown teardown(helper_ctxs, helpers);
  for (S8 helper_idx = 0; helper_idx < num_helpers_; ++helper_idx) {
    SearchContext* ctx = helper_ctxs[helper_idx].get();
    helpers.emplace_back([ctx] { ctx->engine_.GetBestMove(); });
  }

  // Main thread keeps its real time bounds, runs inline, self-stops on soft.
  SearchContext main_context(&tt_, board, pos_history, search_time);
  Move best_move = main_context.engine_.GetBestMove();

  // `teardown` stops the helpers and joins them as it goes out of scope, after
  // best_move is captured.
  return best_move;
}

}  // namespace omegazero
