#include "display.h"

#if FEAT_DISPLAY

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <GxEPD2_BW.h>
#include <SPI.h>

#include "board.h"

// SSD1680 250x122 panel used on the WeAct 2.13" B/W module. If the image is shifted or
// garbled, try GxEPD2_213_BN instead (see docs/hardware-bringup.md).
static GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> epd(
    GxEPD2_213_B74(board::kEpdCs, board::kEpdDc, board::kEpdRst, board::kEpdBusy));

static constexpr int kW = 250, kH = 122, kColX = 168;
static constexpr uint8_t kFullEvery = 20;  // partial refreshes before a cleaning full refresh

bool Display::begin() {
  BusGuard g;
  SPI.begin(board::kSpiSck, board::kSpiMiso, board::kSpiMosi);
  epd.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  epd.init(0, true, 2, false);
  epd.setRotation(1);  // landscape: 250 wide x 122 tall
  epd.setTextColor(GxEPD_BLACK);
  awake_ = true;
  return true;
}

void Display::hibernate() {
  BusGuard g;
  epd.hibernate();
  awake_ = false;
}

static void text(int x, int y, const String& s, const GFXfont* f) {
  epd.setFont(f);
  epd.setCursor(x, y);
  epd.print(s);
}

static String hms(uint32_t s) {
  char b[16];
  if (s >= 3600) {
    snprintf(b, sizeof b, "%u:%02u:%02u", unsigned(s / 3600), unsigned(s / 60 % 60), unsigned(s % 60));
  } else {
    snprintf(b, sizeof b, "%02u:%02u", unsigned(s / 60), unsigned(s % 60));
  }
  return b;
}

void Display::drawStatusColumn(const UiModel& m) {
  epd.drawFastVLine(kColX - 8, 8, kH - 16, GxEPD_BLACK);
  text(kColX, 26, String(m.batteryPct) + "%", &FreeSansBold12pt7b);
  text(kColX, 42, "battery", &FreeSans9pt7b);
  text(kColX, 70, String(m.freeHours, m.freeHours < 10 ? 1 : 0) + "h", &FreeSansBold12pt7b);
  text(kColX, 86, "free", &FreeSans9pt7b);
  String status;
  if (m.hasCamera) {
    if (m.privacy) status = "cam off";
    else if (m.docked) status = "docked";
    else status = m.lightProfile;
  } else if (m.unsynced) {
    status = String(m.unsynced) + " unsynced";
  }
  if (status.length()) text(kColX, 112, status, &FreeSans9pt7b);
}

void Display::drawHome(const UiModel& m) {
  text(10, 52, m.clock, &FreeSansBold18pt7b);
  text(12, 74, m.date, &FreeSans9pt7b);
  if (m.unsynced) text(12, 94, String(m.unsynced) + " to sync", &FreeSans9pt7b);
  text(12, 114, m.footer.length() ? m.footer : String("Tap REC to record"), &FreeSans9pt7b);
  drawStatusColumn(m);
}

void Display::drawRecording(const UiModel& m) {
  epd.fillCircle(20, 30, 9, GxEPD_BLACK);
  text(36, 42, "REC", &FreeSansBold18pt7b);
  text(10, 70, m.recStart + "  " + hms(m.recSeconds), &FreeSans9pt7b);
  String counts = String(m.marks) + " marks";
  if (m.hasCamera) counts += "  " + String(m.photos) + " photos";
  text(10, 92, counts, &FreeSans9pt7b);
  if (m.lowAudio) text(10, 114, "Mic quiet - covered?", &FreeSans9pt7b);
  else if (m.docked) text(10, 114, "Docked: audio only", &FreeSans9pt7b);
  drawStatusColumn(m);
}

void Display::drawSessions(const UiModel& m) {
  text(10, 20, "Sessions", &FreeSansBold12pt7b);
  if (m.sessionCount == 0) text(10, 56, "No recordings yet", &FreeSans9pt7b);
  for (uint8_t i = 0; i < m.sessionCount && i < 5; ++i) {
    const SessionLine& s = m.sessions[i];
    const int y = 42 + i * 18;
    text(10, y, s.label, &FreeSans9pt7b);
    text(130, y, String(s.minutes) + "m", &FreeSans9pt7b);
    if (s.synced) epd.fillCircle(190, y - 5, 4, GxEPD_BLACK);
    else epd.drawCircle(190, y - 5, 4, GxEPD_BLACK);
  }
  text(206, 20, String(m.unsynced), &FreeSans9pt7b);
}

void Display::drawSync(const UiModel& m) {
  text(10, 28, "Wi-Fi sync", &FreeSansBold12pt7b);
  text(10, 56, m.syncLine1, &FreeSans9pt7b);
  text(10, 76, m.syncLine2, &FreeSans9pt7b);
  if (m.syncTotal) {
    const int w = 230 * m.syncDone / m.syncTotal;
    epd.drawRect(10, 92, 230, 14, GxEPD_BLACK);
    epd.fillRect(10, 92, w, 14, GxEPD_BLACK);
  }
}

void Display::drawSettings(const UiModel& m) {
  text(10, 20, "Settings", &FreeSansBold12pt7b);
  for (int i = 0; i < 5; ++i) text(10, 42 + i * 18, m.settings[i], &FreeSans9pt7b);
}

void Display::drawBig(const char* title, const char* l1, const char* l2) {
  text(10, 48, title, &FreeSansBold18pt7b);
  text(12, 80, l1, &FreeSans9pt7b);
  text(12, 102, l2, &FreeSans9pt7b);
}

void Display::render(const UiModel& m, bool full) {
  BusGuard g;
  if (!awake_) {
    epd.init(0, false, 2, false);
    epd.setRotation(1);
    awake_ = true;
  }
  const bool doFull = full || partials_ >= kFullEvery;
  if (doFull) {
    epd.setFullWindow();
    partials_ = 0;
  } else {
    epd.setPartialWindow(0, 0, epd.width(), epd.height());
    ++partials_;
  }
  epd.firstPage();
  do {
    epd.fillScreen(GxEPD_WHITE);
    switch (m.screen) {
      case Screen::Home: drawHome(m); break;
      case Screen::Recording: drawRecording(m); break;
      case Screen::Sessions: drawSessions(m); break;
      case Screen::Sync: drawSync(m); break;
      case Screen::Settings: drawSettings(m); break;
      case Screen::UsbDrive: drawBig("USB drive", "Eject on your computer,", "then unplug."); break;
      case Screen::LowBattery: drawBig("Charge me", "Recording saved.", "Plug in USB-C."); break;
      case Screen::Sleep: drawBig("Asleep", "Touch REC to wake.", m.footer.c_str()); break;
    }
  } while (epd.nextPage());
}

#endif  // FEAT_DISPLAY
