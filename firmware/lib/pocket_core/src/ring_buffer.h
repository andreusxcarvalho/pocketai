// Single-producer / single-consumer byte ring buffer. The audio task writes, the storage
// task reads. Lock-free; capacity must be a power of two. Storage can live in PSRAM.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pocket {

class RingBuffer {
 public:
  RingBuffer() = default;
  // `mem` must outlive the buffer; `capacity` must be a power of two.
  bool init(uint8_t* mem, size_t capacity) {
    if (!mem || capacity == 0 || (capacity & (capacity - 1)) != 0) return false;
    buf_ = mem;
    cap_ = capacity;
    head_.store(0);
    tail_.store(0);
    overruns_.store(0);
    return true;
  }
  size_t capacity() const { return cap_; }
  size_t available() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  size_t space() const { return cap_ - available(); }
  uint32_t overruns() const { return overruns_.load(); }
  // Drops buffered data. Only call while the producer is stopped.
  void clear() {
    tail_.store(head_.load());
    overruns_.store(0);
  }

  // Producer. Writes as much as fits; counts an overrun if data had to be dropped.
  size_t write(const uint8_t* data, size_t len) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    size_t free = cap_ - (head - tail);
    if (len > free) {
      overruns_.fetch_add(1);
      len = free;
    }
    copyIn(head, data, len);
    head_.store(head + len, std::memory_order_release);
    return len;
  }

  // Consumer. Reads up to `len` bytes.
  size_t read(uint8_t* out, size_t len) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t have = head - tail;
    if (len > have) len = have;
    copyOut(tail, out, len);
    tail_.store(tail + len, std::memory_order_release);
    return len;
  }

 private:
  void copyIn(size_t pos, const uint8_t* src, size_t len) {
    const size_t i = pos & (cap_ - 1);
    const size_t first = len < cap_ - i ? len : cap_ - i;
    std::memcpy(buf_ + i, src, first);
    std::memcpy(buf_, src + first, len - first);
  }
  void copyOut(size_t pos, uint8_t* dst, size_t len) const {
    const size_t i = pos & (cap_ - 1);
    const size_t first = len < cap_ - i ? len : cap_ - i;
    std::memcpy(dst, buf_ + i, first);
    std::memcpy(dst + first, buf_, len - first);
  }

  uint8_t* buf_ = nullptr;
  size_t cap_ = 0;
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
  std::atomic<uint32_t> overruns_{0};
};

}  // namespace pocket
