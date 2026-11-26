// Minimal streaming JSON writer for sidecar files (no allocation beyond std::string).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace pocket {

class JsonWriter {
 public:
  JsonWriter& beginObject() { sep(); out_ += '{'; first_ = true; return *this; }
  JsonWriter& endObject() { out_ += '}'; first_ = false; return *this; }
  JsonWriter& beginArray() { sep(); out_ += '['; first_ = true; return *this; }
  JsonWriter& endArray() { out_ += ']'; first_ = false; return *this; }

  JsonWriter& key(const char* k) {
    sep();
    str(k);
    out_ += ':';
    first_ = true;  // value follows without a comma
    return *this;
  }
  JsonWriter& value(const char* s) { sep(); str(s); return *this; }
  JsonWriter& value(const std::string& s) { return value(s.c_str()); }
  JsonWriter& value(bool b) { sep(); out_ += b ? "true" : "false"; return *this; }
  JsonWriter& value(int64_t n) {
    sep();
    char buf[24];
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(n));
    out_ += buf;
    return *this;
  }
  JsonWriter& value(int n) { return value(static_cast<int64_t>(n)); }
  JsonWriter& value(uint32_t n) { return value(static_cast<int64_t>(n)); }
  JsonWriter& value(double d, int decimals = 2) {
    sep();
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, d);
    out_ += buf;
    return *this;
  }
  JsonWriter& null() { sep(); out_ += "null"; return *this; }

  template <typename T>
  JsonWriter& field(const char* k, T v) { key(k); return value(v); }
  JsonWriter& field(const char* k, double v, int decimals) { key(k); return value(v, decimals); }

  const std::string& str() const { return out_; }

 private:
  void sep() {
    if (!first_) out_ += ',';
    first_ = false;
  }
  void str(const char* s) {
    out_ += '"';
    for (; *s; ++s) {
      const char c = *s;
      switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\r': out_ += "\\r"; break;
        case '\t': out_ += "\\t"; break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out_ += buf;
          } else {
            out_ += c;
          }
      }
    }
    out_ += '"';
  }
  std::string out_;
  bool first_ = true;
};

}  // namespace pocket
