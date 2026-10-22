#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <probot/core/lock.hpp>

namespace probot::telemetry {

namespace detail {
  constexpr size_t BUFFER_SIZE = 256;

  struct TelemetryBuffer {
    char data[BUFFER_SIZE];
    volatile uint16_t head = 0;
    volatile uint16_t len = 0;
    volatile uint32_t seq = 0;
  };

  inline TelemetryBuffer g_buffer{};

  // Writers run on the user core, readers on the network core — one
  // short critical section keeps head/len/data consistent (see
  // lock.hpp for why this must not be a raw spinlock on ESP32).
  inline probot::core::Mux g_mux{};

  struct LockGuard {
    LockGuard() { g_mux.lock(); }
    ~LockGuard() { g_mux.unlock(); }
  };
}

inline void writeBytes(const char* msg, size_t msgLen) {
  auto& buf = detail::g_buffer;
  if (msgLen == 0) return;
  detail::LockGuard guard;

  if (msgLen >= detail::BUFFER_SIZE) {
    msg += (msgLen - detail::BUFFER_SIZE);
    msgLen = detail::BUFFER_SIZE;
    buf.head = 0;
    buf.len = 0;
  }

  uint16_t head = buf.head;
  size_t first = detail::BUFFER_SIZE - head;
  if (first > msgLen) first = msgLen;
  memcpy(buf.data + head, msg, first);
  size_t remaining = msgLen - first;
  if (remaining) {
    memcpy(buf.data, msg + first, remaining);
  }
  head = static_cast<uint16_t>((head + msgLen) % detail::BUFFER_SIZE);
  buf.head = head;

  uint16_t newLen = buf.len;
  if (newLen + msgLen >= detail::BUFFER_SIZE) {
    newLen = detail::BUFFER_SIZE;
  } else {
    newLen = static_cast<uint16_t>(newLen + msgLen);
  }
  buf.len = newLen;
  uint32_t s = buf.seq; buf.seq = s + 1;
}

inline void print(const char* msg) {
  writeBytes(msg, strlen(msg));
}

inline void println(const char* msg = "") {
  print(msg);
  print("\n");
}

inline void printf(const char* fmt, ...) {
  char tmp[detail::BUFFER_SIZE];   // the whole panel
  va_list args;
  va_start(args, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end(args);
  print(tmp);
}

inline void clear() {
  auto& buf = detail::g_buffer;
  detail::LockGuard guard;
  buf.head = 0;
  buf.len = 0;
  uint32_t s = buf.seq; buf.seq = s + 1;
}

// Internal: called by the DS. Copies into the caller's buffer (out_size
// must be >= BUFFER_SIZE + 1); returns bytes written excluding the NUL.
inline size_t copyBuffer(char* out, size_t out_size) {
  auto& buf = detail::g_buffer;
  if (out_size == 0) return 0;
  detail::LockGuard guard;
  uint16_t len = buf.len;
  if (len >= out_size) len = static_cast<uint16_t>(out_size - 1);
  if (len == 0) {
    out[0] = '\0';
    return 0;
  }
  uint16_t head = buf.head;
  uint16_t tail = static_cast<uint16_t>((head + detail::BUFFER_SIZE - buf.len) % detail::BUFFER_SIZE);
  size_t first = detail::BUFFER_SIZE - tail;
  if (first > len) first = len;
  memcpy(out, buf.data + tail, first);
  size_t remaining = len - first;
  if (remaining) {
    memcpy(out + first, buf.data, remaining);
  }
  out[len] = '\0';
  return len;
}

// Legacy convenience: returns a static buffer. Not safe if two readers
// call it concurrently — the DS uses copyBuffer() with its own storage.
inline const char* getBuffer() {
  static char out[detail::BUFFER_SIZE + 1];
  copyBuffer(out, sizeof(out));
  return out;
}

inline uint16_t getLength() {
  return detail::g_buffer.len;
}

inline uint32_t getSeq() {
  return detail::g_buffer.seq;
}

} // namespace probot::telemetry

// Convenience shortcuts in probot:: namespace
namespace probot {
  inline void print(const char* msg) { telemetry::print(msg); }
  inline void println(const char* msg = "") { telemetry::println(msg); }
  inline void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  inline void printf(const char* fmt, ...) {
    char tmp[telemetry::detail::BUFFER_SIZE];   // the whole panel
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    telemetry::print(tmp);
  }
  inline void clearTelemetry() { telemetry::clear(); }
}
