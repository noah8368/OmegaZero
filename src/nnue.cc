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

  auto ft_raw = std::make_unique<int16_t[]>(kFtOutSize * kHalfKpSize);
  ft_weight_ = std::make_unique<int16_t[]>(kHalfKpSize * kFtOutSize);
  ft_bias_ = std::make_unique<int16_t[]>(kFtOutSize);
  l2_weight_ = std::make_unique<int8_t[]>(kL2OutSize * kFtOutSize * 2);
  l2_bias_ = std::make_unique<int32_t[]>(kL2OutSize);
  l3_weight_ = std::make_unique<int8_t[]>(kL3OutSize * kL2OutSize);
  l3_bias_ = std::make_unique<int32_t[]>(kL3OutSize);
  output_weight_ = std::make_unique<int8_t[]>(kL3OutSize);

  f.read(reinterpret_cast<char*>(ft_raw.get()),
         kFtOutSize * kHalfKpSize * sizeof(int16_t));
  f.read(reinterpret_cast<char*>(ft_bias_.get()),
         kFtOutSize * sizeof(int16_t));

  // Transpose ft_weight from [kFtOutSize][kHalfKpSize] to [kHalfKpSize][kFtOutSize]
  // so each feature's weights are contiguous in memory.
  for (int j = 0; j < kFtOutSize; ++j)
    for (int idx = 0; idx < kHalfKpSize; ++idx)
      ft_weight_[idx * kFtOutSize + j] = ft_raw[j * kHalfKpSize + idx];
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
  int16_t white_accum[kFtOutSize];
  int16_t black_accum[kFtOutSize];
  ComputeAccumulator(white_king_sq, kWhite, piece_layout, player_layout,
                     white_accum);
  ComputeAccumulator(black_king_sq, kBlack, piece_layout, player_layout,
                     black_accum);
  return ForwardFromAccumulators(white_accum, black_accum, player_to_move);
}

auto NnueNetwork::ComputeAccumulator(S8 king_sq, S8 perspective,
                                     const S8* piece_layout,
                                     const S8* player_layout,
                                     int16_t* accum) const -> void {
  std::memcpy(accum, ft_bias_.get(), kFtOutSize * sizeof(int16_t));

  S8 mapped_king = (perspective == kBlack) ? static_cast<S8>(king_sq ^ 56)
                                           : king_sq;

  for (S8 sq = 0; sq < kNumSq; ++sq) {
    S8 piece = piece_layout[sq];
    S8 player = player_layout[sq];
    if (piece == kNA || piece == kKing) continue;

    S8 mapped_sq = (perspective == kBlack) ? static_cast<S8>(sq ^ 56) : sq;
    int pi = (player == perspective) ? piece : piece + 5;
    int idx = HalfKpIndex(mapped_king, pi, mapped_sq);

    const int16_t* row = &ft_weight_[idx * kFtOutSize];
    for (int j = 0; j < kFtOutSize; ++j)
      accum[j] += row[j];
  }
}

auto NnueNetwork::AddFeature(int halfkp_idx, int16_t* accum) const -> void {
  const int16_t* row = &ft_weight_[halfkp_idx * kFtOutSize];
  for (int j = 0; j < kFtOutSize; ++j)
    accum[j] += row[j];
}

auto NnueNetwork::RemoveFeature(int halfkp_idx, int16_t* accum) const -> void {
  const int16_t* row = &ft_weight_[halfkp_idx * kFtOutSize];
  for (int j = 0; j < kFtOutSize; ++j)
    accum[j] -= row[j];
}

auto NnueNetwork::ForwardFromAccumulators(const int16_t* white_accum,
                                          const int16_t* black_accum,
                                          S8 player_to_move) const -> int {
  const int16_t* first = (player_to_move == kWhite) ? white_accum : black_accum;
  const int16_t* second = (player_to_move == kWhite) ? black_accum : white_accum;

  int8_t concat[kFtOutSize * 2];
  for (int j = 0; j < kFtOutSize; ++j) {
    concat[j] = static_cast<int8_t>(Clamp(first[j], 0, 127));
    concat[kFtOutSize + j] = static_cast<int8_t>(Clamp(second[j], 0, 127));
  }

  int8_t l2_out[kL2OutSize];
  for (int i = 0; i < kL2OutSize; ++i) {
    int32_t sum = l2_bias_[i];
    const int8_t* row = &l2_weight_[i * kFtOutSize * 2];
    for (int j = 0; j < kFtOutSize * 2; ++j)
      sum += static_cast<int32_t>(row[j]) * static_cast<int32_t>(concat[j]);
    l2_out[i] = static_cast<int8_t>(Clamp(sum / kHiddenScale, 0, kFtScale));
  }

  int8_t l3_out[kL3OutSize];
  for (int i = 0; i < kL3OutSize; ++i) {
    int32_t sum = l3_bias_[i];
    const int8_t* row = &l3_weight_[i * kL2OutSize];
    for (int j = 0; j < kL2OutSize; ++j)
      sum += static_cast<int32_t>(row[j]) * static_cast<int32_t>(l2_out[j]);
    l3_out[i] = static_cast<int8_t>(Clamp(sum / kHiddenScale, 0, kFtScale));
  }

  int32_t output = output_bias_;
  for (int i = 0; i < kL3OutSize; ++i)
    output += static_cast<int32_t>(output_weight_[i]) *
              static_cast<int32_t>(l3_out[i]);

  return static_cast<int>(static_cast<int64_t>(output) * 400 / kOutputScale);
}

}  // namespace omegazero
