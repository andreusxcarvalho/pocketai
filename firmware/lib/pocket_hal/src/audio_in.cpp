#include "audio_in.h"

#include <ESP_I2S.h>

#include "board.h"

static I2SClass gI2s;

bool AudioIn::begin(uint32_t sampleRate) {
  gI2s.setPinsPdmRx(board::kMicClk, board::kMicData);
  if (!gI2s.begin(I2S_MODE_PDM_RX, sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    log_e("I2S PDM begin failed");
    return false;
  }
  running_ = true;
  // Throw away the first ~100 ms: the PDM filter settles with a DC thump.
  int16_t scratch[256];
  for (int i = 0; i < 6; ++i) gI2s.readBytes(reinterpret_cast<char*>(scratch), sizeof scratch);
  return true;
}

void AudioIn::end() {
  if (running_) gI2s.end();
  running_ = false;
}

size_t AudioIn::read(int16_t* out, size_t maxSamples) {
  if (!running_) return 0;
  size_t bytes = gI2s.readBytes(reinterpret_cast<char*>(out), maxSamples * sizeof(int16_t));
  size_t n = bytes / sizeof(int16_t);
  int16_t pk = 0;
  for (size_t i = 0; i < n; ++i) {
    int32_t v = static_cast<int32_t>(out[i] * gain_);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    out[i] = static_cast<int16_t>(v);
    int16_t a = v < 0 ? static_cast<int16_t>(-v > 32767 ? 32767 : -v) : static_cast<int16_t>(v);
    if (a > pk) pk = a;
  }
  peak_ = pk;
  return n;
}
