#include "wav.h"

#include <cstring>

namespace pocket {
namespace {

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

size_t buildPcmHeader(uint8_t* out, uint32_t sampleRate, uint16_t channels, uint32_t dataBytes) {
  const uint16_t bits = 16;
  const uint16_t blockAlign = static_cast<uint16_t>(channels * bits / 8);
  std::memcpy(out, "RIFF", 4);
  put32(out + 4, 36 + dataBytes);
  std::memcpy(out + 8, "WAVE", 4);
  std::memcpy(out + 12, "fmt ", 4);
  put32(out + 16, 16);
  put16(out + 20, static_cast<uint16_t>(WavFormat::Pcm));
  put16(out + 22, channels);
  put32(out + 24, sampleRate);
  put32(out + 28, sampleRate * blockAlign);
  put16(out + 32, blockAlign);
  put16(out + 34, bits);
  std::memcpy(out + 36, "data", 4);
  put32(out + 40, dataBytes);
  return kPcmHeaderSize;
}

size_t buildAdpcmHeader(uint8_t* out, uint32_t sampleRate, uint16_t blockAlign,
                        uint16_t samplesPerBlock, uint32_t dataBytes, uint32_t totalSamples) {
  std::memcpy(out, "RIFF", 4);
  put32(out + 4, static_cast<uint32_t>(kAdpcmHeaderSize - 8 + dataBytes));
  std::memcpy(out + 8, "WAVE", 4);
  std::memcpy(out + 12, "fmt ", 4);
  put32(out + 16, 20);
  put16(out + 20, static_cast<uint16_t>(WavFormat::ImaAdpcm));
  put16(out + 22, 1);
  put32(out + 24, sampleRate);
  put32(out + 28, static_cast<uint32_t>(
                      (static_cast<uint64_t>(sampleRate) * blockAlign) / samplesPerBlock));
  put16(out + 32, blockAlign);
  put16(out + 34, 4);
  put16(out + 36, 2);  // cbSize
  put16(out + 38, samplesPerBlock);
  std::memcpy(out + 40, "fact", 4);
  put32(out + 44, 4);
  put32(out + 48, totalSamples);
  std::memcpy(out + 52, "data", 4);
  put32(out + 56, dataBytes);
  return kAdpcmHeaderSize;
}

bool parseWavHeader(const uint8_t* buf, size_t len, WavInfo& info) {
  if (len < 12 || std::memcmp(buf, "RIFF", 4) != 0 || std::memcmp(buf + 8, "WAVE", 4) != 0) {
    return false;
  }
  bool haveFmt = false;
  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t* ck = buf + pos;
    uint32_t size = get32(ck + 4);
    if (std::memcmp(ck, "fmt ", 4) == 0 && pos + 8 + 16 <= len) {
      info.format = static_cast<WavFormat>(get16(ck + 8));
      info.channels = get16(ck + 10);
      info.sampleRate = get32(ck + 12);
      info.blockAlign = get16(ck + 20);
      info.bitsPerSample = get16(ck + 22);
      info.samplesPerBlock = 1;
      if (info.format == WavFormat::ImaAdpcm && size >= 20 && pos + 8 + 20 <= len) {
        info.samplesPerBlock = get16(ck + 26);
      }
      haveFmt = true;
    } else if (std::memcmp(ck, "fact", 4) == 0 && pos + 12 <= len) {
      info.factSamples = get32(ck + 8);
    } else if (std::memcmp(ck, "data", 4) == 0) {
      info.dataOffset = static_cast<uint32_t>(pos + 8);
      info.dataBytes = size;
      return haveFmt;
    }
    pos += 8 + size + (size & 1);
  }
  return false;
}

uint32_t wavDurationMs(const WavInfo& info, uint32_t dataBytes) {
  if (info.sampleRate == 0 || info.blockAlign == 0) return 0;
  uint64_t samples;
  if (info.format == WavFormat::ImaAdpcm) {
    samples = static_cast<uint64_t>(dataBytes / info.blockAlign) * info.samplesPerBlock;
  } else {
    samples = dataBytes / info.blockAlign;
  }
  return static_cast<uint32_t>(samples * 1000 / info.sampleRate);
}

}  // namespace pocket
