/* Noah Himed
 *
 * Define the NNUE evaluation network. Loads quantized weights from a binary
 * file and performs integer-arithmetic inference using the HalfKP architecture.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_NNUE_H_
#define OMEGAZERO_SRC_NNUE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "move.h"

namespace omegazero {

constexpr int kHalfKpSize = 40960;
constexpr int kAccumSize = 256;
constexpr int kL2OutSize = 32;
constexpr int kL3OutSize = 32;

constexpr int kNumPieceTypesHalfKp = 10;
constexpr int kActivationScale = 127;
constexpr int kHiddenScale = 64;
constexpr int kOutputScale = kActivationScale * kHiddenScale;  // 8128

class NnueNetwork {
 public:
  auto Load(const std::string& path) -> bool;
  auto IsLoaded() const -> bool { return loaded_; }

  auto Forward(S8 white_king_sq, S8 black_king_sq,
               const S8* piece_layout, const S8* player_layout,
               S8 player_to_move) const -> int;

  auto ForwardFromAccumulators(const int16_t* white_accum,
                               const int16_t* black_accum,
                               S8 player_to_move) const -> int;

  auto ComputeAccumulator(S8 king_sq, S8 perspective,
                          const S8* piece_layout, const S8* player_layout,
                          int16_t* accum) const -> void;

  auto AddFeature(int halfkp_idx, int16_t* accum) const -> void;
  auto RemoveFeature(int halfkp_idx, int16_t* accum) const -> void;

  auto GetAccumBias() const -> const int16_t* { return accum_bias_.get(); }

 private:
  bool loaded_ = false;

  std::unique_ptr<int16_t[]> accum_weight_;   // [kHalfKpSize][kAccumSize] (transposed at load)
  std::unique_ptr<int16_t[]> accum_bias_;     // [kAccumSize]
  std::unique_ptr<int8_t[]> l2_weight_;    // [kL2OutSize][kAccumSize * 2]
  std::unique_ptr<int32_t[]> l2_bias_;     // [kL2OutSize]
  std::unique_ptr<int8_t[]> l3_weight_;    // [kL3OutSize][kL2OutSize]
  std::unique_ptr<int32_t[]> l3_bias_;     // [kL3OutSize]
  std::unique_ptr<int8_t[]> output_weight_; // [kL3OutSize]
  int32_t output_bias_ = 0;
};

extern NnueNetwork g_nnue;

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_NNUE_H_
