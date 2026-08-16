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
      // search never reads; GetPlayerToMove() returns kWhite/kBlack (0/1),
      // which the Engine ctor would reject as an invalid side. Pass the side to
      // move as its char so construction is valid.
      engine_(tt, &board_, board_.GetPlayerToMove() == kWhite ? 'w' : 'b',
              search_time, pos_history) {}

SearchPool::SearchPool(S8 num_threads) {
  num_helpers_ = num_threads > 0 ? num_threads - 1 : 0;
}

auto SearchPool::StartHelpers(const Board& root, const vector<U64>& pos_history,
                              const SearchParams& params) -> void {
  helper_ctxs_.clear();
  helper_threads_.clear();
  helper_ctxs_.reserve(num_helpers_);
  helper_threads_.reserve(num_helpers_);
  // Placeholder budget; each helper immediately switches to an unbounded search
  // and is ended by StopHelpers() when the main search finishes.
  constexpr float kHelperPlaceholderTime = 1.0f;
  for (S8 helper_idx = 0; helper_idx < num_helpers_; ++helper_idx) {
    helper_ctxs_.push_back(std::make_unique<SearchContext>(
        &tt_, root, pos_history, kHelperPlaceholderTime));
    // Helpers must search with the same parameters as the main engine.
    helper_ctxs_[helper_idx]->engine_.SetParams(params);
    helper_ctxs_[helper_idx]->engine_.SetInfiniteSearch();
  }
  for (S8 i = 0; i < num_helpers_; ++i) {
    SearchContext* ctx = helper_ctxs_[i].get();
    helper_threads_.emplace_back([ctx] { ctx->engine_.GetBestMove(); });
  }
}

auto SearchPool::StopHelpers() -> void {
  for (auto& ctx : helper_ctxs_) {
    ctx->engine_.RequestStop();
  }
  for (auto& thread : helper_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  helper_threads_.clear();
  helper_ctxs_.clear();
}

auto SearchPool::LazySmpSearch(Engine& main, const Board& root,
                               const vector<U64>& pos_history) -> Move {
  StartHelpers(root, pos_history, main.GetParams());
  // Tear the helpers down on scope exit, including if the main search throws.
  HelperTeardown teardown(*this);

  // The caller's already-configured main engine keeps its real time bounds,
  // runs inline, and self-stops on its soft bound (or an external RequestStop).
  return main.GetBestMove();
}

}  // namespace omegazero
