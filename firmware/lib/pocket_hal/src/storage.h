// microSD storage: mounting, session folders, crash-safe WAV writing, repair on boot.
// Every SD call takes the shared SPI bus lock (the e-ink is on the same bus).
#pragma once
#include <Arduino.h>
#include <FS.h>

#include <string>
#include <vector>

#include "ima_adpcm.h"

class Storage {
 public:
  bool begin();
  void end();
  bool mounted() const { return mounted_; }
  uint64_t totalBytes();
  uint64_t usedBytes();
  uint64_t freeBytes() { return totalBytes() - usedBytes(); }

  bool writeText(const String& path, const std::string& text);
  bool readText(const String& path, String& out, size_t maxLen = 8192);
  bool exists(const String& path);
  bool mkdirs(const String& path);
  bool removeTree(const String& path);
  bool writeFile(const String& path, const uint8_t* data, size_t len);

  // Session directory names under /sessions, oldest first (names sort by time).
  std::vector<String> listSessions();
  // Fixes WAV headers and session.json for sessions left in "recording" state by a reset.
  int repairIncompleteSessions();

 private:
  bool mounted_ = false;
};

// Writes 16-bit PCM or IMA-ADPCM WAV. The header is rewritten every `headerEveryMs`
// so a power cut loses at most that much audio.
class WavRecorder {
 public:
  enum class Format { Pcm16, ImaAdpcm };
  bool open(const String& path, Format fmt, uint32_t sampleRate);
  // Push samples; encoding (ADPCM) happens here, on the storage task.
  bool write(const int16_t* samples, size_t count);
  void maybeCheckpoint(uint32_t nowMs, uint32_t headerEveryMs = 10000);
  bool close();
  bool isOpen() const { return open_; }
  uint32_t durationMs() const;
  uint32_t dataBytes() const { return dataBytes_; }
  Format format() const { return fmt_; }

 private:
  bool writeHeader();
  static void onBlock(const uint8_t* block, size_t len, void* ctx);

  fs::File file_;
  bool open_ = false;
  Format fmt_ = Format::Pcm16;
  uint32_t rate_ = 16000;
  uint32_t dataBytes_ = 0;
  uint32_t lastCheckpoint_ = 0;
  bool ioError_ = false;
  pocket::AdpcmStreamEncoder adpcm_{&WavRecorder::onBlock, this};
};
