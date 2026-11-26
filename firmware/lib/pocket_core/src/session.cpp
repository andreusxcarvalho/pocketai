#include "session.h"

#include <cstdio>

#include "json_writer.h"

namespace pocket {

std::string sessionDirName(const DateTime& t, bool clockSynced, uint32_t bootCount,
                           uint32_t sessionIndex) {
  char buf[48];
  if (clockSynced) {
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02u_%02u%02u", t.year, t.month, t.day, t.hour,
                  t.minute);
  } else {
    std::snprintf(buf, sizeof buf, "boot%04u_s%03u", static_cast<unsigned>(bootCount % 10000),
                  static_cast<unsigned>(sessionIndex % 1000));
  }
  return buf;
}

std::string isoTimestamp(const DateTime& t) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02u", t.year, t.month, t.day, t.hour,
                t.minute, t.second);
  return buf;
}

std::string photoName(uint32_t index) {
  char buf[20];
  std::snprintf(buf, sizeof buf, "img_%04u.jpg", static_cast<unsigned>(index));
  return buf;
}

std::string marksJson(const std::vector<Mark>& marks) {
  JsonWriter w;
  w.beginArray();
  for (const auto& m : marks) {
    w.beginObject().field("t_ms", m.tMs);
    if (m.photo.empty()) {
      w.key("photo").null();
    } else {
      w.field("photo", m.photo);
    }
    w.field("source", m.source.empty() ? std::string("tap") : m.source).endObject();
  }
  w.endArray();
  return w.str();
}

std::string photoJson(const PhotoMeta& m) {
  JsonWriter w;
  w.beginObject()
      .field("session", m.session)
      .field("offset_ms", m.offsetMs)
      .field("lux", static_cast<double>(m.lux), 1)
      .field("profile", profileName(m.profile))
      .field("exposure", m.exposure)
      .field("gain", m.gain)
      .field("warmup_frames", static_cast<int>(m.warmupFrames))
      .field("low_light", m.lowLight)
      .field("width", static_cast<int>(m.width))
      .field("height", static_cast<int>(m.height))
      .field("sensor", m.sensor)
      .field("source", m.source.empty() ? std::string("tap") : m.source)
      .endObject();
  return w.str();
}

std::string sessionJson(const SessionMeta& m) {
  JsonWriter w;
  w.beginObject().field("id", m.id);
  if (m.startedAt.empty()) {
    w.key("started_at").null();
  } else {
    w.field("started_at", m.startedAt);
  }
  w.field("clock", m.clockSynced ? "ntp" : "estimated")
      .field("firmware", m.firmware)
      .field("audio_format", m.audioFormat)
      .field("sample_rate", m.sampleRate)
      .field("duration_ms", m.durationMs)
      .field("status", m.status)
      .field("marks", m.marks)
      .field("photos", m.photos)
      .field("overruns", m.overruns)
      .endObject();
  return w.str();
}

}  // namespace pocket
