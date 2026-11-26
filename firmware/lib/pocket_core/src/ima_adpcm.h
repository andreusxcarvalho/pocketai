// IMA ADPCM (Microsoft/DVI flavour, mono) encoder and decoder.
// Pure C++, no Arduino dependencies, so it is unit tested on the host.
//
// Block layout (mono): 4-byte header [int16 first sample][uint8 step index][uint8 0],
// then (blockAlign - 4) bytes of 4-bit codes, low nibble first.
// With blockAlign = 256 a block carries 505 samples: 16 kHz audio becomes ~8.1 KB/s.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pocket {

constexpr uint16_t kAdpcmBlockAlign = 256;
constexpr uint16_t kAdpcmSamplesPerBlock = (kAdpcmBlockAlign - 4) * 2 + 1;  // 505

struct AdpcmState {
  int32_t predictor = 0;
  int32_t index = 0;
};

// Encode one 4-bit code and update the state.
uint8_t adpcmEncodeSample(AdpcmState& st, int16_t sample);
// Decode one 4-bit code and update the state.
int16_t adpcmDecodeSample(AdpcmState& st, uint8_t code);

// Encode exactly kAdpcmSamplesPerBlock samples into one kAdpcmBlockAlign-byte block.
// `st.index` carries over between blocks; the predictor is reset to the block's first sample.
void adpcmEncodeBlock(AdpcmState& st, const int16_t* samples, uint8_t* blockOut);
// Decode one block into kAdpcmSamplesPerBlock samples.
void adpcmDecodeBlock(const uint8_t* block, int16_t* samplesOut);

// Streaming encoder: feed any number of samples; emits full blocks through the callback.
class AdpcmStreamEncoder {
 public:
  using BlockSink = void (*)(const uint8_t* block, size_t len, void* ctx);
  AdpcmStreamEncoder(BlockSink sink, void* ctx) : sink_(sink), ctx_(ctx) {}
  void push(const int16_t* samples, size_t count);
  // Pads the final partial block with silence (repeat of last sample) and emits it.
  // Returns the number of real samples that were in that partial block (0 if none).
  size_t flush();
  uint32_t samplesEncoded() const { return total_; }
  void reset() { fill_ = 0; total_ = 0; st_ = AdpcmState{}; }

 private:
  BlockSink sink_;
  void* ctx_;
  AdpcmState st_;
  int16_t pending_[kAdpcmSamplesPerBlock];
  size_t fill_ = 0;
  uint32_t total_ = 0;
  uint8_t block_[kAdpcmBlockAlign];
};

}  // namespace pocket
