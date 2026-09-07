/* Noah Himed
 *
 * Implement the NNUE evaluation network. Loads quantized HalfKP weights and
 * performs forward inference using integer arithmetic.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "nnue.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "board.h"

namespace omegazero {

using std::cerr;
using std::endl;
using std::ifstream;
using std::ios;
using std::istream;
using std::istringstream;
using std::string;
using std::vector;

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

namespace {

inline auto Md5Rot(uint32_t x, uint32_t c) -> uint32_t {
  return (x << c) | (x >> (32 - c));
}

// One-shot MD5 (RFC 1321), little-endian host assumed -- consistent with the rest
// of this file's raw binary I/O, and matching Python hashlib.md5 so the loader
// reproduces oznu.py's trunk-checksum self-check.
auto Md5(const unsigned char* msg, size_t len, unsigned char digest[16]) -> void {
  static const uint32_t s[64] = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
      5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  static const uint32_t kConst[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
      0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
      0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
      0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
      0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
      0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
      0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

  uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

  size_t padded = ((len + 8) / 64 + 1) * 64;
  vector<unsigned char> buf(padded, 0);
  memcpy(buf.data(), msg, len);
  buf[len] = 0x80;
  uint64_t bits = static_cast<uint64_t>(len) * 8;
  memcpy(buf.data() + padded - 8, &bits, 8);

  for (size_t off = 0; off < padded; off += 64) {
    uint32_t m[16];
    memcpy(m, buf.data() + off, 64);
    uint32_t a = a0, b = b0, c = c0, d = d0;
    for (uint32_t i = 0; i < 64; ++i) {
      uint32_t fn = 0;
      uint32_t g = 0;
      if (i < 16) {
        fn = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        fn = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        fn = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        fn = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      fn = fn + a + kConst[i] + m[g];
      a = d;
      d = c;
      c = b;
      b = b + Md5Rot(fn, s[i]);
    }
    a0 += a;
    b0 += b;
    c0 += c;
    d0 += d;
  }
  memcpy(digest, &a0, 4);
  memcpy(digest + 4, &b0, 4);
  memcpy(digest + 8, &c0, 4);
  memcpy(digest + 12, &d0, 4);
}

}  // namespace

auto NnueNetwork::LoadTrunkStream(istream& f, const string& ctx) -> bool {
  char magic[4];
  f.read(magic, 4);
  if (!f || memcmp(magic, "OZNN", 4) != 0) {
    cerr << "NNUE: invalid magic in " << ctx << endl;
    return false;
  }

  int32_t dims[4];
  f.read(reinterpret_cast<char*>(dims), sizeof(dims));
  if (dims[0] != kHalfKpSize || dims[1] != kAccumSize ||
      dims[2] != kL2OutSize || dims[3] != kL3OutSize) {
    cerr << "NNUE: architecture mismatch in " << ctx << endl;
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
    cerr << "NNUE: truncated file " << ctx << endl;
    loaded_ = false;
    return false;
  }

  loaded_ = true;
  return true;
}

auto NnueNetwork::Load(const string& path) -> bool {
  ifstream f(path, ios::binary);
  if (!f.is_open()) {
    return false;
  }

  char magic[4];
  f.read(magic, 4);
  if (!f) {
    cerr << "NNUE: cannot read " << path << endl;
    return false;
  }

  if (memcmp(magic, "OZNN", 4) == 0) {
    has_head_ = false;
    f.seekg(0);
    return LoadTrunkStream(f, path);
  }
  if (memcmp(magic, "OZNU", 4) == 0) {
    return LoadOznu(f, path);
  }
  cerr << "NNUE: invalid magic in " << path << endl;
  return false;
}

auto NnueNetwork::LoadOznu(istream& f, const string& ctx) -> bool {
  // f is positioned just past the 4-byte "OZNU" magic.
  int32_t version = 0;
  f.read(reinterpret_cast<char*>(&version), sizeof(version));
  if (!f || version != 1) {
    cerr << "OZNU: unsupported version in " << ctx << endl;
    return false;
  }
  int32_t run_id_len = 0;
  f.read(reinterpret_cast<char*>(&run_id_len), sizeof(run_id_len));
  if (!f || run_id_len < 0 || run_id_len > (1 << 16)) {
    cerr << "OZNU: bad run_id length in " << ctx << endl;
    return false;
  }
  string run_id(static_cast<size_t>(run_id_len), '\0');
  if (run_id_len > 0) {
    f.read(&run_id[0], run_id_len);
  }
  unsigned char trunk_md5[16];
  f.read(reinterpret_cast<char*>(trunk_md5), sizeof(trunk_md5));

  int64_t nnue_len = 0;
  int64_t head_len = 0;
  f.read(reinterpret_cast<char*>(&nnue_len), sizeof(nnue_len));
  f.read(reinterpret_cast<char*>(&head_len), sizeof(head_len));
  if (!f || nnue_len <= 0 || head_len <= 0 || nnue_len > (1LL << 30) ||
      head_len > (1LL << 30)) {
    cerr << "OZNU: bad section sizes in " << ctx << endl;
    return false;
  }

  vector<unsigned char> nnue_buf(static_cast<size_t>(nnue_len));
  vector<unsigned char> head_buf(static_cast<size_t>(head_len));
  f.read(reinterpret_cast<char*>(nnue_buf.data()), nnue_len);
  f.read(reinterpret_cast<char*>(head_buf.data()), head_len);
  if (!f) {
    cerr << "OZNU: truncated file " << ctx << endl;
    return false;
  }

  // Self-check: the embedded trunk's md5 must match the header's trunk_md5.
  unsigned char got[16];
  Md5(nnue_buf.data(), static_cast<size_t>(nnue_len), got);
  if (memcmp(got, trunk_md5, sizeof(got)) != 0) {
    cerr << "OZNU: trunk checksum mismatch (corrupt) in " << ctx << endl;
    return false;
  }

  // Load the trunk from the embedded OZNN section.
  string nnue_str(reinterpret_cast<const char*>(nnue_buf.data()),
                  static_cast<size_t>(nnue_len));
  istringstream trunk_ss(nnue_str, ios::in | ios::binary);
  if (!LoadTrunkStream(trunk_ss, ctx)) {
    return false;
  }

  // Parse the uncertainty head; a bad/mismatched head degrades to eval-only
  // rather than disabling the (valid) trunk.
  string head_str(reinterpret_cast<const char*>(head_buf.data()),
                  static_cast<size_t>(head_len));
  istringstream head_ss(head_str, ios::in | ios::binary);
  if (!LoadHeadStream(head_ss, ctx, trunk_md5)) {
    cerr << "OZNU: uncertainty head unreadable; using eval only in " << ctx << endl;
    has_head_ = false;
    return true;
  }
  has_head_ = true;
  head_run_id_ = run_id;
  return true;
}

auto NnueNetwork::LoadHeadStream(istream& f, const string& ctx,
                                 const unsigned char* expected_md5) -> bool {
  char magic[4];
  f.read(magic, 4);
  if (!f || memcmp(magic, "OZUH", 4) != 0) {
    cerr << "OZNU: bad head magic in " << ctx << endl;
    return false;
  }
  int32_t hdr[4];  // version, in_dim, k, n_hidden
  f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
  const int32_t head_version = hdr[0];
  const int32_t in_dim = hdr[1];
  const int32_t k = hdr[2];
  const int32_t n_hidden = hdr[3];
  if (!f || in_dim != 2 * kAccumSize || n_hidden != 2 || k <= 0) {
    cerr << "OZNU: head shape mismatch in " << ctx << endl;
    return false;
  }
  int32_t hidden[2];
  f.read(reinterpret_cast<char*>(hidden), sizeof(hidden));
  float u_mean = 0.0F;
  float u_std = 1.0F;
  f.read(reinterpret_cast<char*>(&u_mean), sizeof(u_mean));
  f.read(reinterpret_cast<char*>(&u_std), sizeof(u_std));
  unsigned char head_md5[16];
  f.read(reinterpret_cast<char*>(head_md5), sizeof(head_md5));
  if (!f) {
    cerr << "OZNU: truncated head in " << ctx << endl;
    return false;
  }
  if (memcmp(head_md5, expected_md5, sizeof(head_md5)) != 0) {
    cerr << "OZNU: head/trunk mismatch in " << ctx << endl;
    return false;
  }
  if (head_version >= 2) {  // skip the free-form run_id string
    int32_t rid_len = 0;
    f.read(reinterpret_cast<char*>(&rid_len), sizeof(rid_len));
    if (!f || rid_len < 0 || rid_len > (1 << 16)) {
      cerr << "OZNU: bad head run_id in " << ctx << endl;
      return false;
    }
    f.ignore(rid_len);
  }

  const int h1 = hidden[0];
  const int h2 = hidden[1];
  const int out2 = 4 * k;
  auto w0 = std::make_unique<float[]>(static_cast<size_t>(h1) * in_dim);
  auto b0 = std::make_unique<float[]>(h1);
  auto w1 = std::make_unique<float[]>(static_cast<size_t>(h2) * h1);
  auto b1 = std::make_unique<float[]>(h2);
  auto w2 = std::make_unique<float[]>(static_cast<size_t>(out2) * h2);
  auto b2 = std::make_unique<float[]>(out2);
  f.read(reinterpret_cast<char*>(w0.get()), sizeof(float) * h1 * in_dim);
  f.read(reinterpret_cast<char*>(b0.get()), sizeof(float) * h1);
  f.read(reinterpret_cast<char*>(w1.get()), sizeof(float) * h2 * h1);
  f.read(reinterpret_cast<char*>(b1.get()), sizeof(float) * h2);
  f.read(reinterpret_cast<char*>(w2.get()), sizeof(float) * out2 * h2);
  f.read(reinterpret_cast<char*>(b2.get()), sizeof(float) * out2);
  if (!f) {
    cerr << "OZNU: truncated head weights in " << ctx << endl;
    return false;
  }

  head_in_dim_ = in_dim;
  head_k_ = k;
  head_h1_ = h1;
  head_h2_ = h2;
  head_u_mean_ = u_mean;
  head_u_std_ = u_std;
  head_w0_ = std::move(w0);
  head_b0_ = std::move(b0);
  head_w1_ = std::move(w1);
  head_b1_ = std::move(b1);
  head_w2_ = std::move(w2);
  head_b2_ = std::move(b2);
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
