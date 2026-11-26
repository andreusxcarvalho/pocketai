#include "storage.h"

#include <SD.h>
#include <SPI.h>

#include <algorithm>

#include "board.h"
#include "wav.h"

using pocket::WavInfo;

bool Storage::begin() {
  BusGuard g;
  SPI.begin(board::kSpiSck, board::kSpiMiso, board::kSpiMosi);
  mounted_ = SD.begin(board::kSdCs, SPI, 20000000, "/sd", 8);
  if (!mounted_) {
    log_e("SD mount failed: check card (FAT32) and seating");
    return false;
  }
  SD.mkdir("/sessions");
  SD.mkdir("/sync");
  return true;
}

void Storage::end() {
  BusGuard g;
  if (mounted_) SD.end();
  mounted_ = false;
}

uint64_t Storage::totalBytes() { BusGuard g; return mounted_ ? SD.totalBytes() : 0; }
uint64_t Storage::usedBytes() { BusGuard g; return mounted_ ? SD.usedBytes() : 0; }

bool Storage::writeText(const String& path, const std::string& text) {
  return writeFile(path, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool Storage::writeFile(const String& path, const uint8_t* data, size_t len) {
  BusGuard g;
  // Write to a temp file then rename, so a reset never leaves a half-written JSON.
  String tmp = path + ".tmp";
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  size_t n = f.write(data, len);
  f.close();
  if (n != len) return false;
  SD.remove(path);
  return SD.rename(tmp, path);
}

bool Storage::readText(const String& path, String& out, size_t maxLen) {
  BusGuard g;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  out = "";
  out.reserve(std::min<size_t>(f.size(), maxLen));
  while (f.available() && out.length() < maxLen) out += static_cast<char>(f.read());
  f.close();
  return true;
}

bool Storage::exists(const String& path) { BusGuard g; return SD.exists(path); }

bool Storage::mkdirs(const String& path) {
  BusGuard g;
  String acc;
  int start = 1;
  while (true) {
    int slash = path.indexOf('/', start);
    acc = slash < 0 ? path : path.substring(0, slash);
    if (!SD.exists(acc) && !SD.mkdir(acc)) return false;
    if (slash < 0) return true;
    start = slash + 1;
  }
}

bool Storage::removeTree(const String& path) {
  BusGuard g;
  File dir = SD.open(path);
  if (!dir) return false;
  if (!dir.isDirectory()) {
    dir.close();
    return SD.remove(path);
  }
  std::vector<String> children;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    children.push_back(path + "/" + e.name());
    e.close();
  }
  dir.close();
  for (auto& c : children) removeTree(c);
  return SD.rmdir(path);
}

std::vector<String> Storage::listSessions() {
  BusGuard g;
  std::vector<String> out;
  File dir = SD.open("/sessions");
  if (!dir) return out;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    if (e.isDirectory()) out.push_back(String(e.name()));
    e.close();
  }
  dir.close();
  std::sort(out.begin(), out.end(), [](const String& a, const String& b) { return a < b; });
  return out;
}

int Storage::repairIncompleteSessions() {
  int repaired = 0;
  for (const String& id : listSessions()) {
    const String base = "/sessions/" + id;
    String meta;
    if (!readText(base + "/session.json", meta)) continue;
    if (meta.indexOf("\"status\":\"recording\"") < 0) continue;

    BusGuard g;
    File f = SD.open(base + "/audio.wav", "r+");
    if (!f) continue;
    uint8_t hdr[64];
    size_t got = f.read(hdr, sizeof hdr);
    WavInfo info;
    if (got >= 44 && pocket::parseWavHeader(hdr, got, info)) {
      uint32_t data = static_cast<uint32_t>(f.size()) - info.dataOffset;
      uint8_t fixed[64];
      size_t hl;
      if (info.format == pocket::WavFormat::ImaAdpcm) {
        data -= data % info.blockAlign;  // drop a torn final block
        uint32_t samples = (data / info.blockAlign) * info.samplesPerBlock;
        hl = pocket::buildAdpcmHeader(fixed, info.sampleRate, info.blockAlign,
                                      info.samplesPerBlock, data, samples);
      } else {
        data -= data % 2;
        hl = pocket::buildPcmHeader(fixed, info.sampleRate, 1, data);
      }
      f.seek(0);
      f.write(fixed, hl);
      info.dataBytes = data;
      meta.replace("\"status\":\"recording\"", "\"status\":\"repaired\"");
      int d = meta.indexOf("\"duration_ms\":");
      if (d >= 0) {
        int end = meta.indexOf(',', d);
        meta = meta.substring(0, d) + "\"duration_ms\":" +
               String(pocket::wavDurationMs(info, data)) + meta.substring(end);
      }
    }
    f.close();
    writeText(base + "/session.json", std::string(meta.c_str()));
    ++repaired;
    log_w("repaired session %s", id.c_str());
  }
  return repaired;
}

// ---------------- WavRecorder ----------------

bool WavRecorder::open(const String& path, Format fmt, uint32_t sampleRate) {
  BusGuard g;
  file_ = SD.open(path, FILE_WRITE);
  if (!file_) return false;
  fmt_ = fmt;
  rate_ = sampleRate;
  dataBytes_ = 0;
  ioError_ = false;
  adpcm_.reset();
  open_ = writeHeader();
  lastCheckpoint_ = millis();
  return open_;
}

void WavRecorder::onBlock(const uint8_t* block, size_t len, void* ctx) {
  auto* self = static_cast<WavRecorder*>(ctx);
  if (self->file_.write(block, len) != len) self->ioError_ = true;
  self->dataBytes_ += static_cast<uint32_t>(len);
}

bool WavRecorder::write(const int16_t* samples, size_t count) {
  if (!open_) return false;
  BusGuard g;
  if (fmt_ == Format::ImaAdpcm) {
    adpcm_.push(samples, count);
  } else {
    const size_t bytes = count * sizeof(int16_t);
    if (file_.write(reinterpret_cast<const uint8_t*>(samples), bytes) != bytes) ioError_ = true;
    dataBytes_ += static_cast<uint32_t>(bytes);
  }
  return !ioError_;
}

bool WavRecorder::writeHeader() {
  uint8_t hdr[pocket::kAdpcmHeaderSize];
  size_t len;
  if (fmt_ == Format::ImaAdpcm) {
    len = pocket::buildAdpcmHeader(hdr, rate_, pocket::kAdpcmBlockAlign,
                                   pocket::kAdpcmSamplesPerBlock, dataBytes_,
                                   (dataBytes_ / pocket::kAdpcmBlockAlign) * pocket::kAdpcmSamplesPerBlock);
  } else {
    len = pocket::buildPcmHeader(hdr, rate_, 1, dataBytes_);
  }
  const size_t pos = file_.position();
  file_.seek(0);
  const bool ok = file_.write(hdr, len) == len;
  file_.seek(pos > len ? pos : len);
  return ok;
}

void WavRecorder::maybeCheckpoint(uint32_t nowMs, uint32_t everyMs) {
  if (!open_ || nowMs - lastCheckpoint_ < everyMs) return;
  BusGuard g;
  writeHeader();
  file_.flush();
  lastCheckpoint_ = nowMs;
}

bool WavRecorder::close() {
  if (!open_) return false;
  BusGuard g;
  if (fmt_ == Format::ImaAdpcm) adpcm_.flush();
  writeHeader();
  file_.close();
  open_ = false;
  return !ioError_;
}

uint32_t WavRecorder::durationMs() const {
  if (fmt_ == Format::ImaAdpcm) return static_cast<uint32_t>(uint64_t(adpcm_.samplesEncoded()) * 1000 / rate_);
  return static_cast<uint32_t>(uint64_t(dataBytes_ / 2) * 1000 / rate_);
}
