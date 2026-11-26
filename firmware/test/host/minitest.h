// Tiny test harness for host-side unit tests (no external dependencies).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace minitest {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}

struct Registrar {
  Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline int runAll() {
  int passed = 0;
  for (auto& c : registry()) {
    const int before = failures();
    c.fn();
    if (failures() == before) {
      ++passed;
    } else {
      std::printf("FAIL  %s\n", c.name);
    }
  }
  std::printf("%d/%zu tests passed, %d failed checks\n", passed, registry().size(), failures());
  return failures() == 0 ? 0 : 1;
}

}  // namespace minitest

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name)                                                              \
  static void MT_CAT(test_, name)();                                            \
  static minitest::Registrar MT_CAT(reg_, name)(#name, MT_CAT(test_, name));    \
  static void MT_CAT(test_, name)()

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::printf("  %s:%d CHECK(%s)\n", __FILE__, __LINE__, #cond);            \
      ++minitest::failures();                                                   \
    }                                                                           \
  } while (0)

#define CHECK_EQ(a, b)                                                          \
  do {                                                                          \
    auto va_ = (a);                                                             \
    auto vb_ = (b);                                                             \
    if (!(va_ == vb_)) {                                                        \
      std::printf("  %s:%d CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, #a, #b);    \
      ++minitest::failures();                                                   \
    }                                                                           \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                   \
  do {                                                                          \
    double va_ = (a), vb_ = (b);                                                \
    if (std::fabs(va_ - vb_) > (tol)) {                                         \
      std::printf("  %s:%d CHECK_NEAR(%s=%g, %s=%g)\n", __FILE__, __LINE__, #a, va_, #b, vb_); \
      ++minitest::failures();                                                   \
    }                                                                           \
  } while (0)
