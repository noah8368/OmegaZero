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

// Bounds for the (optional) uncertainty head, so its forward can use stack
// buffers. The trained MDNt is k=5, hidden (128,128); these leave headroom.
constexpr int kMaxMixture = 16;      // mixture components (k)
constexpr int kHeadMaxHidden = 256;  // per hidden layer width
constexpr int kHeadMaxOut = 4 * kMaxMixture;

// The conditional eval-error distribution p(u | x) predicted by the uncertainty
// head, where u = v - v_star (signed static-eval error, cp). A k-component
// Student-t mixture in STANDARDIZED space; de-standardize with (u_mean, u_std).
// See unc_research/experiments/unc-007.md and train_unc_head.py (MDNt).
struct UncDist {
  int v_cp = 0;     // NNUE eval (STM POV, cp) -- the point estimate the dist is around
  int k = 0;        // mixture components (0 => no head loaded, only v_cp valid)
  float u_mean = 0.0F;
  float u_std = 1.0F;
  float pi[kMaxMixture] = {};     // mixture weights (sum to 1)
  float mu[kMaxMixture] = {};     // component locations (standardized)
  float sigma[kMaxMixture] = {};  // component scales (standardized)
  float df[kMaxMixture] = {};     // component degrees of freedom

  // Mean error E[u | x] in cp (the correction; a distributional generalization
  // of correction history -- see notes/correction_history.md).
  auto MeanCp() const -> float;
  // Mixture CDF P(U <= u_cp | x).
  auto Cdf(float u_cp) const -> float;
  // Inverse CDF (one-sided margin primitive): the tau-quantile of u, in cp.
  auto QuantileCp(float tau) const -> float;
};

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

  // Eval + the uncertainty head's p(u | x), computed off the SAME shared
  // accumulators as the eval (H5). If no head is loaded, only `v_cp` is set.
  auto EvalWithDistribution(const int16_t* white_accum,
                            const int16_t* black_accum,
                            S8 player_to_move) const -> UncDist;

  // Fast int8 mean-only corrector: E[u | x] in cp (STM POV) -- the correction
  // term the search subtracts from the raw eval (unc-008 H6/G). Same value as
  // EvalWithDistribution().MeanCp() up to int8 quantization error, but runs the
  // two big MLP layers on the integer path and computes only the logits/mu
  // outputs (softmax + weighted sum in float), skipping sigma/df and all
  // quantile machinery. Returns 0 if no head is loaded.
  auto MeanCorrectionCp(const int16_t* white_accum, const int16_t* black_accum,
                        S8 player_to_move) const -> int;

  auto ComputeAccumulator(S8 king_sq, S8 perspective,
                          const S8* piece_layout, const S8* player_layout,
                          int16_t* accum) const -> void;

  auto AddFeature(int halfkp_idx, int16_t* accum) const -> void;
  auto RemoveFeature(int halfkp_idx, int16_t* accum) const -> void;

  auto GetAccumBias() const -> const int16_t* { return accum_bias_.get(); }

  // Uncertainty head: present only when Load() was given a fused OZNU file.
  auto HasHead() const -> bool { return has_head_; }
  auto GetHeadRunId() const -> const std::string& { return head_run_id_; }

 private:
  // Load the quantized HalfKP trunk (magic + dims + weights) from a stream; used
  // both for a bare OZNN file and for the OZNN section embedded in an OZNU file.
  auto LoadTrunkStream(std::istream& f, const std::string& ctx) -> bool;
  // Parse a fused OZNU container: validate header + md5 self-check, load the trunk
  // from the embedded OZNN section, and parse the OZUH uncertainty head.
  auto LoadOznu(std::istream& f, const std::string& ctx) -> bool;
  // Parse the OZUH head section (float MDN weights) from a stream, verifying its
  // baked trunk_md5 matches `expected_md5` (the fused file's trunk).
  auto LoadHeadStream(std::istream& f, const std::string& ctx,
                      const unsigned char* expected_md5) -> bool;

  bool loaded_ = false;

  std::unique_ptr<int16_t[]> accum_weight_;   // [kHalfKpSize][kAccumSize] (transposed at load)
  std::unique_ptr<int16_t[]> accum_bias_;     // [kAccumSize]
  std::unique_ptr<int8_t[]> l2_weight_;    // [kL2OutSize][kAccumSize * 2]
  std::unique_ptr<int32_t[]> l2_bias_;     // [kL2OutSize]
  std::unique_ptr<int8_t[]> l3_weight_;    // [kL3OutSize][kL2OutSize]
  std::unique_ptr<int32_t[]> l3_bias_;     // [kL3OutSize]
  std::unique_ptr<int8_t[]> output_weight_; // [kL3OutSize]
  int32_t output_bias_ = 0;

  // Uncertainty head (float MDN over the frozen 512-dim trunk embedding),
  // populated only from a fused OZNU file. Layers: in_dim->h1->h2->(4*k). See
  // unc_research/scripts/oznu.py (OZNU) and train_unc_head.py (OZUH / MDNt).
  bool has_head_ = false;
  int head_in_dim_ = 0;
  int head_k_ = 0;
  int head_h1_ = 0;
  int head_h2_ = 0;
  float head_u_mean_ = 0.0F;
  float head_u_std_ = 1.0F;
  std::string head_run_id_;
  std::unique_ptr<float[]> head_w0_;  // [h1][in_dim]
  std::unique_ptr<float[]> head_b0_;  // [h1]
  std::unique_ptr<float[]> head_w1_;  // [h2][h1]
  std::unique_ptr<float[]> head_b1_;  // [h2]
  std::unique_ptr<float[]> head_w2_;  // [4k][h2]
  std::unique_ptr<float[]> head_b2_;  // [4k]
};

extern NnueNetwork g_nnue;

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_NNUE_H_
