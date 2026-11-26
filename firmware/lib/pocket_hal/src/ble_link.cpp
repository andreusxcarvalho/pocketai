#include "ble_link.h"

#if FEAT_BLE

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static const char* kService = "7a1e0001-5c6f-4b8e-9f1a-0c0ffee00001";
static const char* kStatus = "7a1e0002-5c6f-4b8e-9f1a-0c0ffee00001";
static const char* kControl = "7a1e0003-5c6f-4b8e-9f1a-0c0ffee00001";

static BLEServer* gServer = nullptr;
static BLECharacteristic* gStatus = nullptr;
static QueueHandle_t gCmds = nullptr;
static volatile bool gConnected = false;

struct Cmd { char text[48]; };

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { gConnected = true; }
  void onDisconnect(BLEServer* s) override {
    gConnected = false;
    s->startAdvertising();  // stay discoverable
  }
};

class ControlCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    Cmd cmd = {};
    String v = c->getValue();
    strncpy(cmd.text, v.c_str(), sizeof(cmd.text) - 1);
    if (gCmds) xQueueSend(gCmds, &cmd, 0);
  }
};

void BleLink::begin(const char* name) {
  if (!gCmds) gCmds = xQueueCreate(8, sizeof(Cmd));
  BLEDevice::init(name);
  gServer = BLEDevice::createServer();
  gServer->setCallbacks(new ServerCb());
  BLEService* svc = gServer->createService(kService);
  gStatus = svc->createCharacteristic(
      kStatus, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  gStatus->addDescriptor(new BLE2902());
  gStatus->setValue("{}");
  BLECharacteristic* ctl = svc->createCharacteristic(
      kControl, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  ctl->setCallbacks(new ControlCb());
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(kService);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void BleLink::end() {
  if (gServer) BLEDevice::deinit(false);
  gServer = nullptr;
  gStatus = nullptr;
}

String BleLink::takeCommand() {
  Cmd cmd;
  if (gCmds && xQueueReceive(gCmds, &cmd, 0) == pdTRUE) return String(cmd.text);
  return String();
}

void BleLink::publishStatus(const String& json) {
  if (!gStatus) return;
  gStatus->setValue(json.c_str());
  if (gConnected) gStatus->notify();
}

bool BleLink::connected() const { return gConnected; }

#endif  // FEAT_BLE
