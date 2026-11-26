#include "sync_client.h"

#if FEAT_SYNC

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

#include <vector>

#include "board.h"
#include "config.h"
#include "power.h"
#include "storage.h"

#if FEAT_OTA
#include <HTTPUpdate.h>
#endif

namespace {

String sha256File(const String& path, uint32_t& size) {
  BusGuard g;
  File f = SD.open(path, FILE_READ);
  if (!f) return "";
  size = f.size();
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  static uint8_t buf[4096];
  while (true) {
    int n = f.read(buf, sizeof buf);
    if (n <= 0) break;
    mbedtls_sha256_update(&ctx, buf, n);
  }
  f.close();
  uint8_t out[32];
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", out[i]);
  return String(hex);
}

void addHeaders(HTTPClient& http, const DeviceConfig& cfg) {
  if (cfg.token.length()) http.addHeader("Authorization", "Bearer " + cfg.token);
  http.addHeader("X-Device", cfg.deviceName);
}

std::vector<String> listFiles(const String& dir) {
  BusGuard g;
  std::vector<String> out;
  File d = SD.open(dir);
  if (!d) return out;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    String n = e.name();
    if (!e.isDirectory() && !n.endsWith(".tmp")) out.push_back(n);
    e.close();
  }
  d.close();
  return out;
}

}  // namespace

std::set<String> SyncClient::loadSynced(Storage& storage) {
  std::set<String> s;
  String text;
  if (!storage.readText("/sync/synced.txt", text, 65536)) return s;
  int start = 0;
  while (start < static_cast<int>(text.length())) {
    int nl = text.indexOf('\n', start);
    if (nl < 0) nl = text.length();
    String line = text.substring(start, nl);
    line.trim();
    if (line.length()) s.insert(line);
    start = nl + 1;
  }
  return s;
}

static void saveSynced(Storage& storage, const std::set<String>& s) {
  std::string out;
  for (const auto& id : s) {
    out += id.c_str();
    out += '\n';
  }
  storage.writeText("/sync/synced.txt", out);
}

static bool sessionFinished(Storage& storage, const String& id) {
  String meta;
  if (!storage.readText("/sessions/" + id + "/session.json", meta)) return false;
  return meta.indexOf("\"status\":\"recording\"") < 0;
}

uint16_t SyncClient::countUnsynced(Storage& storage) {
  auto synced = loadSynced(storage);
  uint16_t n = 0;
  for (const auto& id : storage.listSessions()) {
    if (!synced.count(id)) ++n;
  }
  return n;
}

bool SyncClient::connectWifi(const DeviceConfig& cfg, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.deviceName.c_str());
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

bool SyncClient::uploadSession(Storage& storage, const DeviceConfig& cfg, const String& id,
                               uint32_t& bytes) {
  const String dir = "/sessions/" + id;
  JsonDocument manifest;
  JsonObject files = manifest["files"].to<JsonObject>();
  for (const String& name : listFiles(dir)) {
    uint32_t size = 0;
    const String path = dir + "/" + name;
    const String sha = sha256File(path, size);
    if (sha.isEmpty()) return false;
    HTTPClient http;
    http.begin(cfg.server + "/api/v1/sessions/" + id + "/files/" + name);
    http.setTimeout(30000);
    addHeaders(http, cfg);
    http.addHeader("X-Sha256", sha);
    http.addHeader("Content-Type", "application/octet-stream");
    int code;
    {
      BusGuard g;  // hold the SD card while HTTPClient streams from it
      File f = SD.open(path, FILE_READ);
      if (!f) return false;
      code = http.sendRequest("PUT", &f, size);
      f.close();
    }
    http.end();
    if (code != 200 && code != 201) {
      log_e("upload %s/%s failed: %d", id.c_str(), name.c_str(), code);
      return false;
    }
    files[name] = sha;
    bytes += size;
  }
  HTTPClient http;
  http.begin(cfg.server + "/api/v1/sessions/" + id + "/complete");
  addHeaders(http, cfg);
  http.addHeader("Content-Type", "application/json");
  String body;
  serializeJson(manifest, body);
  const int code = http.POST(body);
  http.end();
  return code == 200;
}

void SyncClient::cleanup(Storage& storage, const DeviceConfig& cfg, const std::set<String>& synced,
                         SyncReport& r) {
  for (const String& id : storage.listSessions()) {  // oldest first
    const uint64_t total = storage.totalBytes();
    if (total == 0 || storage.usedBytes() * 100 / total < cfg.cleanupPercent) break;
    if (!synced.count(id)) continue;  // never delete anything the inbox hasn't confirmed
    if (storage.removeTree("/sessions/" + id)) ++r.sessionsDeleted;
  }
}

SyncReport SyncClient::run(Storage& storage, const DeviceConfig& cfg, Progress progress) {
  SyncReport r;
  if (!cfg.hasWifi()) {
    r.error = "No Wi-Fi in config.json";
    progress(r.error, "", 0, 0);
    return r;
  }
  progress("Joining " + cfg.wifiSsid, "", 0, 0);
  r.wifi = connectWifi(cfg, 15000);
  if (!r.wifi) {
    r.error = "Wi-Fi not found";
    progress(r.error, cfg.wifiSsid, 0, 0);
    WiFi.mode(WIFI_OFF);
    return r;
  }
  progress("Setting clock", WiFi.localIP().toString(), 0, 0);
  r.clockSet = clockx::syncNtp(8000);

  // Ask the inbox what it already has; it is the source of truth.
  std::set<String> synced = loadSynced(storage);
  {
    HTTPClient http;
    http.begin(cfg.server + "/api/v1/sessions");
    addHeaders(http, cfg);
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) {
        for (JsonVariant v : doc["complete"].as<JsonArray>()) synced.insert(String(v.as<const char*>()));
      }
    } else {
      r.error = "Inbox unreachable";
      progress(r.error, cfg.server, 0, 0);
      http.end();
      WiFi.mode(WIFI_OFF);
      return r;
    }
    http.end();
  }

  std::vector<String> todo;
  for (const String& id : storage.listSessions()) {
    if (!synced.count(id) && sessionFinished(storage, id)) todo.push_back(id);
  }
  uint16_t done = 0;
  for (const String& id : todo) {
    progress("Uploading", id, done, todo.size());
    uint32_t bytes = 0;
    if (uploadSession(storage, cfg, id, bytes)) {
      synced.insert(id);
      saveSynced(storage, synced);
      ++r.sessionsUploaded;
      r.bytesSent += bytes;
    } else {
      ++r.sessionsFailed;
    }
    ++done;
  }
  cleanup(storage, cfg, synced, r);
  saveSynced(storage, synced);
  progress(String(r.sessionsUploaded) + " uploaded, " + String(r.sessionsFailed) + " failed",
           r.sessionsDeleted ? String(r.sessionsDeleted) + " old sessions cleared" : String(""),
           done, todo.size());
#if !FEAT_OTA
  WiFi.mode(WIFI_OFF);
#endif
  return r;
}

#if FEAT_OTA
bool SyncClient::checkOta(const DeviceConfig& cfg, Progress progress) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin(cfg.server + "/api/v1/firmware/latest?variant=" + pocketVersionName() +
             "&current=" + POCKET_FW_SEMVER);
  addHeaders(http, cfg);
  const int code = http.GET();
  if (code != 200) {
    http.end();
    WiFi.mode(WIFI_OFF);
    return false;  // 204 = up to date
  }
  JsonDocument doc;
  deserializeJson(doc, http.getString());
  http.end();
  const String version = doc["version"] | "";
  const String url = doc["url"] | "";
  if (version.isEmpty() || url.isEmpty() || version == POCKET_FW_SEMVER) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  progress("Updating firmware", version, 0, 1);
  WiFiClient client;
  httpUpdate.rebootOnUpdate(true);
  const String full = url.startsWith("http") ? url : cfg.server + url;
  HTTPUpdateResult res = httpUpdate.update(client, full);
  if (res == HTTP_UPDATE_FAILED) progress("Update failed", httpUpdate.getLastErrorString(), 0, 1);
  WiFi.mode(WIFI_OFF);
  return false;
}
#endif

#endif  // FEAT_SYNC
