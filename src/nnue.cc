/* Noah Himed
 *
 * Implement the NNUE evaluation network. Loads quantized HalfKP weights and
 * performs forward inference using integer arithmetic.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "nnue.h"

#include <cstring>
#include <fstream>
#include <iostream>

#include "board.h"

namespace omegazero {

NnueNetwork g_nnue;

static inline int Clamp(int val, int lo, int hi) {
  if (val < lo) return lo;
  if (val > hi) return hi;
  return val;
}

static int HalfKpIndex(S8 king_sq, int piece_index, S8 piece_sq) {
  return king_sq * (kNumPieceTypesHalfKp * kNumSq) +
         piece_index * kNumSq + piece_sq;
}

auto NnueNetwork::Load(const std::string& path) -> bool {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;

  char magic[4];
  f.read(magic, 4);
  if (std::memcmp(magic, "OZNN", 4) != 0) {
    std::cerr << "NNUE: invalid magic in " << path << std::endl;
    return false;
  }

  int32_t dims[4];
  f.read(reinterpret_cast<char*>(dims), sizeof(dims));
  if (dims[0] != kHalfKpSize || dims[1] != kFtOutSize ||
      dims[2] != kL2OutSize || dims[3] != kL3OutSize) {
    std::cerr << "NNUE: architecture mismatch in " << path << std::endl;
    return false;
  }

  ft_weight_ = std::make_unique<int16_t[]>(kFtOutSize * kHalfKpSize);
  ft_bias_ = std::make_unique<int16_t[]>(kFtOutSize);
  l2_weight_ = std::make_unique<int8_t[]>(kL2OutSize * kFtOutSize * 2);
  l2_bias_ = std::make_unique<int32_t[]>(kL2OutSize);
  l3_weight_ = std::make_unique<int8_t[]>(kL3OutSize * kL2OutSize);
  l3_bias_ = std::make_unique<int32_t[]>(kL3OutSize);
  output_weight_ = std::make_unique<int8_t[]>(kL3OutSize);

  f.read(reinterpret_cast<char*>(ft_weight_.get()),
         kFtOutSize * kHalfKpSize * sizeof(int16_t));
  f.read(reinterpret_cast<char*>(ft_bias_.get()),
         kFtOutSize * sizeof(int16_t));
  f.read(reinterpret_cast<char*>(l2_weight_.get()),
         kL2OutSize * kFtOutSize * 2 * sizeof(int8_t));
  f.read(reinterpret_cast<char*>(l2_bias_.get()),
         kL2OutSize * sizeof(int32_t));
  f.read(reinterpret_cast<char*>(l3_weight_.get()),
         kL3OutSize * kL2OutSize * sizeof(int8_t));
  f.read(reinterpret_cast<char*>(l3_bias_.get()),
         kL3OutSize * sizeof(int32_t));
  f.read(reinterpret_cast<char*>(output_weight_.get()),
         kL3OutSize * sizeof(int8_t));
  f.read(reinterpret_cast<char*>(&output_bias_), sizeof(int32_t));

  if (!f) {
    std::cerr << "NNUE: truncated file " << path << std::endl;
    loaded_ = false;
    return false;
  }

  loaded_ = true;
  return true;
}

auto NnueNetwork::Forward(S8 white_king_sq, S8 black_king_sq,
                           const S8* piece_layout, const S8* player_layout,
                           S8 player_to_move) const -> int {
  // Feature transformer: accumulate active features into accumulators.
  int16_t white_accum[kFtOutSize];
  int16_t black_accum[kFtOutSize];
  std::memcpy(white_accum, ft_bias_.get(), kFtOutSize * sizeof(int16_t));
  std::memcpy(black_accum, ft_bias_.get(), kFtOutSize * sizeof(int16_t));

  S8 mirrored_black_king = static_cast<S8>(black_king_sq ^ 56);

  for (S8 sq = 0; sq < kNumSq; ++sq) {
    S8 piece = piece_layout[sq];
    S8 player = player_layout[sq];
    if (piece == kNA || piece == kKing) continue;

    // White perspective: friendly pieces 0-4, enemy pieces 5-9.
    int pi_white = (player == kWhite) ? piece : piece + 5;
    int w_idx = HalfKpIndex(white_king_sq, pi_white, sq);
    for (int j = 0; j < kFtOutSize; ++j) {
      white_accum[j] += ft_weight_[static_cast<size_t>(j) * kHalfKpSize + w_idx];
    }

    // Black perspective: mirror squares, swap colors.
    S8 mirrored_sq = static_cast<S8>(sq ^ 56);
    int pi_black = (player == kBlack) ? piece : piece + 5;
    int b_idx = HalfKpIndex(mirrored_black_king, pi_black, mirrored_sq);
    for (int j = 0; j < kFtOutSize; ++j) {
      black_accum[j] += ft_weight_[static_cast<size_t>(j) * kHalfKpSize + b_idx];
    }
  }

  // ClippedReLU: clamp to [0, 127] → int8.
  int8_t stm_accum[kFtOutSize];
  int8_t other_accum[kFtOutSize];
  const int16_t* first = (player_to_move == kWhite) ? white_accum : black_accum;
  const int16_t* second = (player_to_move == kWhite) ? black_accum : white_accum;
  for (int j = 0; j < kFtOutSize; ++j) {
    stm_accum[j] = static_cast<int8_t>(Clamp(first[j], 0, 127));
    other_accum[j] = static_cast<int8_t>(Clamp(second[j], 0, 127));
  }

  // Concatenate: [stm_perspective, other_perspective].
  int8_t concat[kFtOutSize * 2];
  std::memcpy(concat, stm_accum, kFtOutSize);
  std::memcpy(concat + kFtOutSize, other_accum, kFtOutSize);

  // L2: int8 weights × int8 input → int32, then ClippedReLU.
  int8_t l2_out[kL2OutSize];
  for (int i = 0; i < kL2OutSize; ++i) {
    int32_t sum = l2_bias_[i];
    const int8_t* row = &l2_weight_[i * kFtOutSize * 2];
    for (int j = 0; j < kFtOutSize * 2; ++j) {
      sum += static_cast<int32_t>(row[j]) * static_cast<int32_t>(concat[j]);
    }
    l2_out[i] = static_cast<int8_t>(
        Clamp(sum / kHiddenScale, 0, kFtScale));
  }

  // L3: same pattern.
  int8_t l3_out[kL3OutSize];
  for (int i = 0; i < kL3OutSize; ++i) {
    int32_t sum = l3_bias_[i];
    const int8_t* row = &l3_weight_[i * kL2OutSize];
    for (int j = 0; j < kL2OutSize; ++j) {
      sum += static_cast<int32_t>(row[j]) * static_cast<int32_t>(l2_out[j]);
    }
    l3_out[i] = static_cast<int8_t>(
        Clamp(sum / kHiddenScale, 0, kFtScale));
  }

  // Output layer: single scalar.
  int32_t output = output_bias_;
  for (int i = 0; i < kL3OutSize; ++i) {
    output += static_cast<int32_t>(output_weight_[i]) *
              static_cast<int32_t>(l3_out[i]);
  }

  // Convert from quantized output to centipawns.
  // Output is at scale kOutputScale (8128). The network learned logits where
  // sigmoid(logit) ≈ win probability, trained with SCORE_SCALE = 400.
  // Multiply by 400 and divide by scale to recover centipawns.
  return static_cast<int>(static_cast<int64_t>(output) * 400 / kOutputScale);
}

}  // namespace omegazero
