#include "ima_adpcm.h"

#include <cstring>

namespace pocket {
namespace {

const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

const int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

inline int32_t clamp16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
inline int32_t clampIndex(int32_t i) { return i < 0 ? 0 : (i > 88 ? 88 : i); }

}  // namespace

uint8_t adpcmEncodeSample(AdpcmState& st, int16_t sample) {
  int32_t diff = static_cast<int32_t>(sample) - st.predictor;
  uint8_t code = 0;
  if (diff < 0) {
    code = 8;
    diff = -diff;
  }
  int32_t step = kStepTable[st.index];
  int32_t vpdiff = step >> 3;
  if (diff >= step) { code |= 4; diff -= step; vpdiff += step; }
  step >>= 1;
  if (diff >= step) { code |= 2; diff -= step; vpdiff += step; }
  step >>= 1;
  if (diff >= step) { code |= 1; vpdiff += step; }
  st.predictor = clamp16((code & 8) ? st.predictor - vpdiff : st.predictor + vpdiff);
  st.index = clampIndex(st.index + kIndexTable[code]);
  return code;
}

int16_t adpcmDecodeSample(AdpcmState& st, uint8_t code) {
  int32_t step = kStepTable[st.index];
  int32_t vpdiff = step >> 3;
  if (code & 4) vpdiff += step;
  if (code & 2) vpdiff += step >> 1;
  if (code & 1) vpdiff += step >> 2;
  st.predictor = clamp16((code & 8) ? st.predictor - vpdiff : st.predictor + vpdiff);
  st.index = clampIndex(st.index + kIndexTable[code & 0x0F]);
  return static_cast<int16_t>(st.predictor);
}

void adpcmEncodeBlock(AdpcmState& st, const int16_t* samples, uint8_t* out) {
  st.predictor = samples[0];
  out[0] = static_cast<uint8_t>(samples[0] & 0xFF);
  out[1] = static_cast<uint8_t>((samples[0] >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>(st.index);
  out[3] = 0;
  uint8_t* p = out + 4;
  for (size_t i = 1; i < kAdpcmSamplesPerBlock; i += 2) {
    uint8_t lo = adpcmEncodeSample(st, samples[i]);
    uint8_t hi = adpcmEncodeSample(st, samples[i + 1]);
    *p++ = static_cast<uint8_t>(lo | (hi << 4));
  }
}

void adpcmDecodeBlock(const uint8_t* block, int16_t* out) {
  AdpcmState st;
  st.predictor = static_cast<int16_t>(block[0] | (block[1] << 8));
  st.index = clampIndex(block[2]);
  out[0] = static_cast<int16_t>(st.predictor);
  const uint8_t* p = block + 4;
  for (size_t i = 1; i < kAdpcmSamplesPerBlock; i += 2) {
    uint8_t b = *p++;
    out[i] = adpcmDecodeSample(st, b & 0x0F);
    out[i + 1] = adpcmDecodeSample(st, (b >> 4) & 0x0F);
  }
}

void AdpcmStreamEncoder::push(const int16_t* samples, size_t count) {
  while (count > 0) {
    size_t take = kAdpcmSamplesPerBlock - fill_;
    if (take > count) take = count;
    std::memcpy(pending_ + fill_, samples, take * sizeof(int16_t));
    fill_ += take;
    samples += take;
    count -= take;
    total_ += static_cast<uint32_t>(take);
    if (fill_ == kAdpcmSamplesPerBlock) {
      adpcmEncodeBlock(st_, pending_, block_);
      sink_(block_, kAdpcmBlockAlign, ctx_);
      fill_ = 0;
    }
  }
}

size_t AdpcmStreamEncoder::flush() {
  if (fill_ == 0) return 0;
  size_t real = fill_;
  int16_t last = pending_[fill_ - 1];
  while (fill_ < kAdpcmSamplesPerBlock) pending_[fill_++] = last;
  adpcmEncodeBlock(st_, pending_, block_);
  sink_(block_, kAdpcmBlockAlign, ctx_);
  fill_ = 0;
  return real;
}

}  // namespace pocket
