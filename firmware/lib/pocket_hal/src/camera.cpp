#include "camera.h"

#if FEAT_CAMERA

#include <SD.h>
#include <esp_camera.h>
#include <img_converters.h>

#include "board.h"

using pocket::CameraSettings;
using pocket::FrameSize;

static framesize_t toEsp(FrameSize f) {
  switch (f) {
    case FrameSize::SVGA_800x600: return FRAMESIZE_SVGA;
    case FrameSize::UXGA_1600x1200: return FRAMESIZE_UXGA;
    case FrameSize::QXGA_2048x1536: return FRAMESIZE_QXGA;
  }
  return FRAMESIZE_UXGA;
}

static gainceiling_t toGain(uint8_t x) {
  if (x >= 128) return GAINCEILING_128X;
  if (x >= 64) return GAINCEILING_64X;
  if (x >= 32) return GAINCEILING_32X;
  if (x >= 16) return GAINCEILING_16X;
  if (x >= 8) return GAINCEILING_8X;
  if (x >= 4) return GAINCEILING_4X;
  return GAINCEILING_2X;
}

bool Camera::begin() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = board::kCamY2; c.pin_d1 = board::kCamY3; c.pin_d2 = board::kCamY4;
  c.pin_d3 = board::kCamY5; c.pin_d4 = board::kCamY6; c.pin_d5 = board::kCamY7;
  c.pin_d6 = board::kCamY8; c.pin_d7 = board::kCamY9;
  c.pin_xclk = board::kCamXclk; c.pin_pclk = board::kCamPclk;
  c.pin_vsync = board::kCamVsync; c.pin_href = board::kCamHref;
  c.pin_sccb_sda = board::kCamSiod; c.pin_sccb_scl = board::kCamSioc;
  c.pin_pwdn = board::kCamPwdn; c.pin_reset = board::kCamReset;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QXGA;     // allocate for the largest frame we will use
  c.jpeg_quality = 10;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;  // always the newest frame, so warm-up frames are real
  if (esp_camera_init(&c) != ESP_OK) {
    log_e("camera init failed");
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  pid_ = s ? s->id.PID : 0;
  if (s) {
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_lenc(s, 1);
    if (pid_ == OV3660_PID || pid_ == OV5640_PID) s->set_vflip(s, 1);  // mounting orientation
  }
  ready_ = true;
  return true;
}

void Camera::end() {
  if (ready_) esp_camera_deinit();
  ready_ = false;
}

const char* Camera::sensorName() const {
  switch (pid_) {
    case OV2640_PID: return "OV2640";
    case OV3660_PID: return "OV3660";
    case OV5640_PID: return "OV5640";
    default: return "unknown";
  }
}

void Camera::apply(const CameraSettings& st) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_framesize(s, toEsp(st.frameSize));
  s->set_quality(s, st.jpegQuality);
  s->set_aec2(s, st.aec2 ? 1 : 0);
  s->set_ae_level(s, st.aeLevel);
  s->set_gainceiling(s, toGain(st.gainCeilingX));
  s->set_wb_mode(s, static_cast<int>(st.wb));
}

void Camera::readExposure(uint32_t& exposure, uint32_t& gain) {
  exposure = gain = 0;
  sensor_t* s = esp_camera_sensor_get();
  if (!s || !s->get_reg) return;
  if (pid_ == OV3660_PID || pid_ == OV5640_PID) {
    // AEC exposure 0x3500-0x3502 (20 bits, 1/16 line units), AGC gain 0x350A-0x350B.
    exposure = ((s->get_reg(s, 0x3500, 0x0F) << 16) | (s->get_reg(s, 0x3501, 0xFF) << 8) |
                s->get_reg(s, 0x3502, 0xFF)) >> 4;
    gain = ((s->get_reg(s, 0x350A, 0x03) << 8) | s->get_reg(s, 0x350B, 0xFF));
  }
}

void Camera::standby(bool on) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s || !s->set_reg) return;
  if (pid_ == OV3660_PID || pid_ == OV5640_PID) {
    s->set_reg(s, 0x3008, 0x40, on ? 0x40 : 0x00);  // SYSTEM CTRL0 bit6: software power down
  } else if (pid_ == OV2640_PID) {
    s->set_reg(s, 0x109, 0x10, on ? 0x10 : 0x00);     // sensor bank COM2 bit4: standby
  }
  if (!on) delay(5);
}

CaptureResult Camera::captureToFile(const CameraSettings& st, const String& path) {
  CaptureResult r;
  if (!ready_ || !st.capture) return r;
  standby(false);
  apply(st);
  for (uint8_t i = 0; i < st.warmupFrames; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    standby(true);
    return r;
  }
  readExposure(r.exposure, r.gain);
  {
    BusGuard g;
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      r.bytes = f.write(fb->buf, fb->len);
      f.close();
      r.ok = r.bytes == fb->len;
    }
  }
  r.width = fb->width;
  r.height = fb->height;
  esp_camera_fb_return(fb);
  standby(true);
  return r;
}

bool Camera::captureGray(std::vector<uint8_t>& gray, uint16_t& w, uint16_t& h) {
  if (!ready_) return false;
  sensor_t* s = esp_camera_sensor_get();
  standby(false);
  s->set_framesize(s, FRAMESIZE_QQVGA);
  camera_fb_t* fb = esp_camera_fb_get();  // first frame after a size change can be stale
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  if (!fb) return false;
  w = fb->width;
  h = fb->height;
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
  const bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, rgb.data());
  esp_camera_fb_return(fb);
  if (!ok) return false;
  gray.resize(static_cast<size_t>(w) * h);
  for (size_t i = 0; i < gray.size(); ++i) {
    const uint8_t* p = &rgb[i * 3];
    gray[i] = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
  }
  return true;
}

#endif  // FEAT_CAMERA
