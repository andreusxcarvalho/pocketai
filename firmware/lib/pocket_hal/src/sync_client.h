// Wi-Fi sync to the Raspberry Pi inbox (pipeline/pocketai/inbox). Protocol:
//   GET  {server}/api/v1/sessions                      -> {"complete": ["id", ...]}
//   PUT  {server}/api/v1/sessions/{id}/files/{name}    body = file, header X-Sha256
//   POST {server}/api/v1/sessions/{id}/complete        {"files": {"name": "sha256", ...}}
//   GET  {server}/api/v1/firmware/latest?variant=...   (OTA, v10)
// All requests carry "Authorization: Bearer {token}" and "X-Device: {name}".
#pragma once
#include <Arduino.h>

#include <functional>
#include <set>

#include "pocket_features.h"

#if FEAT_SYNC

class Storage;
struct DeviceConfig;

struct SyncReport {
  bool wifi = false;
  bool clockSet = false;
  uint16_t sessionsUploaded = 0;
  uint16_t sessionsFailed = 0;
  uint16_t sessionsDeleted = 0;
  uint32_t bytesSent = 0;
  String error;
};

class SyncClient {
 public:
  using Progress = std::function<void(const String& line1, const String& line2, uint16_t done, uint16_t total)>;
  SyncReport run(Storage& storage, const DeviceConfig& cfg, Progress progress);
  // Number of finished sessions not yet confirmed by the inbox.
  static uint16_t countUnsynced(Storage& storage);
  static std::set<String> loadSynced(Storage& storage);

#if FEAT_OTA
  // Checks the inbox for newer firmware and flashes it. Reboots on success.
  bool checkOta(const DeviceConfig& cfg, Progress progress);
#endif

 private:
  bool connectWifi(const DeviceConfig& cfg, uint32_t timeoutMs);
  bool uploadSession(Storage& storage, const DeviceConfig& cfg, const String& id, uint32_t& bytes);
  void cleanup(Storage& storage, const DeviceConfig& cfg, const std::set<String>& synced, SyncReport& r);
};

#endif
