// WAV header building and parsing (PCM 16-bit and IMA ADPCM), host-testable.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pocket {

enum class WavFormat : uint16_t { Pcm = 0x0001, ImaAdpcm = 0x0011 };

constexpr size_t kPcmHeaderSize = 44;
constexpr size_t kAdpcmHeaderSize = 60;  // RIFF + fmt(20) + fact + data

struct WavInfo {
  WavFormat format = WavFormat::Pcm;
  uint16_t channels = 1;
  uint32_t sampleRate = 16000;
  uint16_t bitsPerSample = 16;
  uint16_t blockAlign = 2;
  uint16_t samplesPerBlock = 1;  // ADPCM only
  uint32_t dataOffset = 0;       // byte offset of the first audio byte
  uint32_t dataBytes = 0;        // as declared in the header
  uint32_t factSamples = 0;      // ADPCM only
};

// Writes a 44-byte header for 16-bit PCM. Returns bytes written.
size_t buildPcmHeader(uint8_t* out, uint32_t sampleRate, uint16_t channels, uint32_t dataBytes);

// Writes a 60-byte header for mono IMA ADPCM with the given block size.
size_t buildAdpcmHeader(uint8_t* out, uint32_t sampleRate, uint16_t blockAlign,
                        uint16_t samplesPerBlock, uint32_t dataBytes, uint32_t totalSamples);

// Parses RIFF/WAVE chunks. Returns false if the buffer isn't a WAV header we understand.
bool parseWavHeader(const uint8_t* buf, size_t len, WavInfo& info);

// Duration in milliseconds for a given number of audio data bytes.
uint32_t wavDurationMs(const WavInfo& info, uint32_t dataBytes);

}  // namespace pocket
