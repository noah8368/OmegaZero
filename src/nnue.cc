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

using std::cerr;
using std::endl;
using std::ifstream;
using std::ios;
using std::string;

NnueNetwork g_nnue;

static inline int Clamp(int val, int lo, int hi) {
  if (val < lo) {
    return lo;
  }
  if (val > hi) {
    return hi;
  }
  return val;
}

static int HalfKpIndex(S8 king_sq, int piece_index, S8 piece_sq) {
  return king_sq * (kNumPieceTypesHalfKp * kNumSq) +
         piece_index * kNumSq + piece_sq;
}

auto NnueNetwork::Load(const string& path) -> bool {
  ifstream f(path, ios::binary);
  if (!f.is_open()) {
    return false;
  }

  char magic[4];
  f.read(magic, 4);
  if (memcmp(magic, "OZNN", 4) != 0) {
    cerr << "NNUE: invalid magic in " << path << endl;
    return false;
  }

  int32_t dims[4];
  f.read(reinterpret_cast<char*>(dims), sizeof(dims));
  if (dims[0] != kHalfKpSize || dims[1] != kAccumSize ||
      dims[2] != kL2OutSize || dims[3] != kL3OutSize) {
    cerr << "NNUE: architecture mismatch in " << path << endl;
    return false;
  }

  auto raw_weight = std::make_unique<int16_t[]>(kAccumSize * kHalfKpSize);
  accum_weight_ = std::make_unique<int16_t[]>(kHalfKpSize * kAccumSize);
  accum_bias_ = std::make_unique<int16_t[]>(kAccumSize);
  l2_weight_ = std::make_unique<int8_t[]>(kL2OutSize * kAccumSize * 2);
  l2_bias_ = std::make_unique<int32_t[]>(kL2OutSize);
  l3_weight_ = std::make_unique<int8_t[]>(kL3OutSize * kL2OutSize);
  l3_bias_ = std::make_unique<int32_t[]>(kL3OutSize);
  output_weight_ = std::make_unique<int8_t[]>(kL3OutSize);

  f.read(reinterpret_cast<char*>(raw_weight.get()),
         kAccumSize * kHalfKpSize * sizeof(int16_t));
  f.read(reinterpret_cast<char*>(accum_bias_.get()),
         kAccumSize * sizeof(int16_t));

  // Transpose accum_weight from [kAccumSize][kHalfKpSize] to [kHalfKpSize][kAccumSize]
  // so each feature's weights are contiguous in memory.
  for (int accum_idx = 0; accum_idx < kAccumSize; ++accum_idx) {
    for (int feature_idx = 0; feature_idx < kHalfKpSize; ++feature_idx) {
      accum_weight_[feature_idx * kAccumSize + accum_idx] = raw_weight[accum_idx * kHalfKpSize + feature_idx];
    }
  }
  f.read(reinterpret_cast<char*>(l2_weight_.get()),
         kL2OutSize * kAccumSize * 2 * sizeof(int8_t));
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
    cerr << "NNUE: truncated file " << path << endl;
    loaded_ = false;
    return false;
  }

  loaded_ = true;
  return true;
}

auto NnueNetwork::Forward(S8 white_king_sq, S8 black_king_sq,
                           const S8* piece_layout, const S8* player_layout,
                           S8 player_to_move) const -> int {
  int16_t white_accum[kAccumSize];
  int16_t black_accum[kAccumSize];
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
  memcpy(accum, accum_bias_.get(), kAccumSize * sizeof(int16_t));

  S8 mapped_king = (perspective == kBlack) ? static_cast<S8>(king_sq ^ 56)
                                           : king_sq;

  for (S8 sq = 0; sq < kNumSq; ++sq) {
    S8 piece = piece_layout[sq];
    S8 player = player_layout[sq];
    if (piece == kNA || piece == kKing) {
      continue;
    }

    S8 mapped_sq = (perspective == kBlack) ? static_cast<S8>(sq ^ 56) : sq;
    int pi = (player == perspective) ? piece : piece + 5;
    int idx = HalfKpIndex(mapped_king, pi, mapped_sq);

    const int16_t* row = &accum_weight_[idx * kAccumSize];
    for (int accum_idx = 0; accum_idx < kAccumSize; ++accum_idx) {
      accum[accum_idx] += row[accum_idx];
    }
  }
}

auto NnueNetwork::AddFeature(int halfkp_idx, int16_t* accum) const -> void {
  const int16_t* row = &accum_weight_[halfkp_idx * kAccumSize];
  for (int accum_idx = 0; accum_idx < kAccumSize; ++accum_idx) {
    accum[accum_idx] += row[accum_idx];
  }
}

auto NnueNetwork::RemoveFeature(int halfkp_idx, int16_t* accum) const -> void {
  const int16_t* row = &accum_weight_[halfkp_idx * kAccumSize];
  for (int accum_idx = 0; accum_idx < kAccumSize; ++accum_idx) {
    accum[accum_idx] -= row[accum_idx];
  }
}

auto NnueNetwork::ForwardFromAccumulators(const int16_t* white_accum,
                                          const int16_t* black_accum,
                                          S8 player_to_move) const -> int {
  const int16_t* first = (player_to_move == kWhite) ? white_accum : black_accum;
  const int16_t* second = (player_to_move == kWhite) ? black_accum : white_accum;

  int8_t concat[kAccumSize * 2];
  for (int accum_idx = 0; accum_idx < kAccumSize; ++accum_idx) {
    concat[accum_idx] = static_cast<int8_t>(Clamp(first[accum_idx], 0, 127));
    concat[kAccumSize + accum_idx] = static_cast<int8_t>(Clamp(second[accum_idx], 0, 127));
  }

  int8_t l2_out[kL2OutSize];
  for (int neuron_idx = 0; neuron_idx < kL2OutSize; ++neuron_idx) {
    int32_t sum = l2_bias_[neuron_idx];
    const int8_t* row = &l2_weight_[neuron_idx * kAccumSize * 2];
    for (int input_idx = 0; input_idx < kAccumSize * 2; ++input_idx) {
      sum += static_cast<int32_t>(row[input_idx]) * static_cast<int32_t>(concat[input_idx]);
    }
    l2_out[neuron_idx] = static_cast<int8_t>(Clamp(sum / kHiddenScale, 0, kActivationScale));
  }

  int8_t l3_out[kL3OutSize];
  for (int neuron_idx = 0; neuron_idx < kL3OutSize; ++neuron_idx) {
    int32_t sum = l3_bias_[neuron_idx];
    const int8_t* row = &l3_weight_[neuron_idx * kL2OutSize];
    for (int input_idx = 0; input_idx < kL2OutSize; ++input_idx) {
      sum += static_cast<int32_t>(row[input_idx]) * static_cast<int32_t>(l2_out[input_idx]);
    }
    l3_out[neuron_idx] = static_cast<int8_t>(Clamp(sum / kHiddenScale, 0, kActivationScale));
  }

  int32_t output = output_bias_;
  for (int neuron_idx = 0; neuron_idx < kL3OutSize; ++neuron_idx) {
    output += static_cast<int32_t>(output_weight_[neuron_idx]) *
              static_cast<int32_t>(l3_out[neuron_idx]);
  }

  return static_cast<int>(static_cast<int64_t>(output) * 400 / kOutputScale);
}

}  // namespace omegazero
