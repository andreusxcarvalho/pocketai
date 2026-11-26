#include "usb_drive.h"

#if FEAT_USB_DRIVE

#if ARDUINO_USB_MODE
#error "v08+ needs TinyUSB: build with -DARDUINO_USB_MODE=0 (set in platformio.ini)"
#endif

#include <SD.h>
#include <SPI.h>
#include <USB.h>
#include <USBMSC.h>

#include "board.h"

static USBMSC gMsc;
static volatile bool gHost = false;
static volatile bool gEjected = false;

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  BusGuard g;
  uint8_t* out = static_cast<uint8_t*>(buffer);
  const uint32_t secs = bufsize / 512;
  for (uint32_t i = 0; i < secs; ++i) {
    if (!SD.readRAW(out + i * 512, lba + i)) return -1;
  }
  (void)offset;
  return static_cast<int32_t>(bufsize);
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  BusGuard g;
  const uint32_t secs = bufsize / 512;
  for (uint32_t i = 0; i < secs; ++i) {
    if (!SD.writeRAW(buffer + i * 512, lba + i)) return -1;
  }
  (void)offset;
  return static_cast<int32_t>(bufsize);
}

static bool onStartStop(uint8_t power, bool start, bool loadEject) {
  if (loadEject && !start) gEjected = true;
  (void)power;
  return true;
}

static void onUsbEvent(void*, esp_event_base_t base, int32_t id, void*) {
  if (base != ARDUINO_USB_EVENTS) return;
  if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT) gHost = true;
  if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT) gHost = false;
}

void UsbDrive::begin() {
  USB.onEvent(onUsbEvent);
}

bool UsbDrive::hostConnected() const { return gHost; }
bool UsbDrive::ejected() const { return gEjected; }

bool UsbDrive::start() {
  BusGuard g;
  // Raw sector access needs the card initialised but the FAT layer idle; SD.begin() gives us
  // both, and the app promises not to open files until stop().
  SPI.begin(board::kSpiSck, board::kSpiMiso, board::kSpiMosi);
  if (!SD.begin(board::kSdCs, SPI, 20000000)) return false;
  const uint32_t sectors = static_cast<uint32_t>(SD.cardSize() / 512);
  gEjected = false;
  gMsc.vendorID("Pocket");
  gMsc.productID("Recorder");
  gMsc.productRevision("1.0");
  gMsc.onRead(onRead);
  gMsc.onWrite(onWrite);
  gMsc.onStartStop(onStartStop);
  gMsc.mediaPresent(true);
  active_ = gMsc.begin(sectors, 512);
  return active_;
}

void UsbDrive::stop() {
  if (!active_) return;
  gMsc.mediaPresent(false);
  gMsc.end();
  BusGuard g;
  SD.end();
  active_ = false;
}

#endif  // FEAT_USB_DRIVE
