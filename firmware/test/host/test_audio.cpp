#include <cmath>
#include <cstring>
#include <vector>

#include "ima_adpcm.h"
#include "minitest.h"
#include "ring_buffer.h"
#include "wav.h"

using namespace pocket;

static std::vector<int16_t> sine(size_t n, double freq, double amp, double rate = 16000) {
  std::vector<int16_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<int16_t>(amp * std::sin(2 * M_PI * freq * i / rate));
  return v;
}

TEST(adpcm_block_roundtrip_is_close) {
  auto in = sine(kAdpcmSamplesPerBlock, 440, 8000);
  AdpcmState st;
  uint8_t block[kAdpcmBlockAlign];
  adpcmEncodeBlock(st, in.data(), block);
  int16_t out[kAdpcmSamplesPerBlock];
  adpcmDecodeBlock(block, out);
  CHECK_EQ(out[0], in[0]);  // first sample stored raw
  double err = 0, sig = 0;
  for (size_t i = 50; i < kAdpcmSamplesPerBlock; ++i) {  // skip adaptation warm-up
    err += std::pow(out[i] - in[i], 2);
    sig += std::pow(in[i], 2);
  }
  const double snrDb = 10 * std::log10(sig / (err + 1e-9));
  CHECK(snrDb > 20.0);
}

TEST(adpcm_stream_emits_whole_blocks_and_counts_samples) {
  struct Sink { std::vector<uint8_t> bytes; size_t blocks = 0; } sink;
  AdpcmStreamEncoder enc(
      [](const uint8_t* b, size_t len, void* ctx) {
        auto* s = static_cast<Sink*>(ctx);
        s->bytes.insert(s->bytes.end(), b, b + len);
        ++s->blocks;
      },
      &sink);
  auto in = sine(1200, 300, 5000);
  enc.push(in.data(), 700);
  enc.push(in.data() + 700, 500);
  CHECK_EQ(sink.blocks, size_t(2));        // 1200 / 505 = 2 full blocks
  CHECK_EQ(enc.flush(), size_t(1200 - 1010));
  CHECK_EQ(sink.blocks, size_t(3));
  CHECK_EQ(sink.bytes.size(), size_t(3 * kAdpcmBlockAlign));
  CHECK_EQ(enc.samplesEncoded(), uint32_t(1200));
}

TEST(adpcm_handles_full_scale_and_silence) {
  std::vector<int16_t> in(kAdpcmSamplesPerBlock);
  for (size_t i = 0; i < in.size(); ++i) in[i] = (i / 20) % 2 ? 32767 : -32768;
  AdpcmState st;
  uint8_t block[kAdpcmBlockAlign];
  adpcmEncodeBlock(st, in.data(), block);
  int16_t out[kAdpcmSamplesPerBlock];
  adpcmDecodeBlock(block, out);
  CHECK(st.index >= 0 && st.index <= 88);
  std::vector<int16_t> zeros(kAdpcmSamplesPerBlock, 0);
  AdpcmState z;
  adpcmEncodeBlock(z, zeros.data(), block);
  adpcmDecodeBlock(block, out);
  for (size_t i = 0; i < kAdpcmSamplesPerBlock; ++i) CHECK(std::abs(out[i]) < 16);
}

TEST(pcm_header_roundtrip) {
  uint8_t h[kPcmHeaderSize];
  CHECK_EQ(buildPcmHeader(h, 16000, 1, 32000), kPcmHeaderSize);
  WavInfo info;
  CHECK(parseWavHeader(h, sizeof h, info));
  CHECK(info.format == WavFormat::Pcm);
  CHECK_EQ(info.sampleRate, uint32_t(16000));
  CHECK_EQ(info.dataOffset, uint32_t(44));
  CHECK_EQ(info.dataBytes, uint32_t(32000));
  CHECK_EQ(wavDurationMs(info, 32000), uint32_t(1000));
}

TEST(adpcm_header_roundtrip) {
  uint8_t h[kAdpcmHeaderSize];
  CHECK_EQ(buildAdpcmHeader(h, 16000, kAdpcmBlockAlign, kAdpcmSamplesPerBlock, 256 * 100, 50500),
           kAdpcmHeaderSize);
  WavInfo info;
  CHECK(parseWavHeader(h, sizeof h, info));
  CHECK(info.format == WavFormat::ImaAdpcm);
  CHECK_EQ(info.samplesPerBlock, uint16_t(505));
  CHECK_EQ(info.blockAlign, uint16_t(256));
  CHECK_EQ(info.factSamples, uint32_t(50500));
  CHECK_EQ(info.dataOffset, uint32_t(60));
  CHECK_EQ(wavDurationMs(info, 256 * 100), uint32_t(50500 * 1000 / 16000));
}

TEST(parse_rejects_garbage) {
  uint8_t junk[64] = {0};
  WavInfo info;
  CHECK(!parseWavHeader(junk, sizeof junk, info));
}

TEST(ring_buffer_wraps_and_reports_overrun) {
  static uint8_t mem[16];
  RingBuffer rb;
  CHECK(!rb.init(mem, 12));  // not a power of two
  CHECK(rb.init(mem, 16));
  uint8_t data[10], out[16];
  for (int i = 0; i < 10; ++i) data[i] = static_cast<uint8_t>(i);
  CHECK_EQ(rb.write(data, 10), size_t(10));
  CHECK_EQ(rb.read(out, 6), size_t(6));
  CHECK_EQ(rb.write(data, 10), size_t(10));  // wraps around
  CHECK_EQ(rb.available(), size_t(14));
  CHECK_EQ(rb.read(out, 16), size_t(14));
  CHECK_EQ(out[0], uint8_t(6));
  CHECK_EQ(out[4], uint8_t(0));
  CHECK_EQ(out[13], uint8_t(9));
  CHECK_EQ(rb.write(data, 10), size_t(10));
  CHECK_EQ(rb.write(data, 10), size_t(6));   // only 6 fit
  CHECK_EQ(rb.overruns(), uint32_t(1));
}
