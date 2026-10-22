#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <probot/core/lock.hpp>

// Robot console: an append-only log of leveled lines that the Driver Station
// shows on its Logs page. Unlike the telemetry panel (a screen the sketch
// clears and redraws), lines stay until the ring is full and the oldest
// ones are overwritten, so warnings and errors survive across loops.
//
// Storage is a byte ring of whole records, allocated once: lazily on the
// first line, then resized by runtime_setup() to PROBOT_CONSOLE_BYTES. Each
// record is [u8 level][u32 ms][u16 len][text]; the sequence number of a
// record is implied by its position (firstSeq + index).

namespace probot::console {

  enum class Level : uint8_t { Info = 0, Warning = 1, Error = 2 };

  namespace detail {
    constexpr size_t   kMaxLine     = 512;    // text bytes per line; longer text is split
    constexpr size_t   kRecordHead  = 7;      // level, ms, len
    constexpr size_t   kDefaultBytes = 4096;
    constexpr size_t   kMinBytes = 2048, kMaxBytes = 65536;
    constexpr uint8_t  FRAME_LINES  = 0x4C;   // 'L'
    constexpr size_t   kFrameHead   = 9;      // 'L', u32 boot, u32 firstSeq
    constexpr size_t   kWireRecordHead = 11;  // u32 seq, u32 ms, u8 level, u16 len
    // Smallest frame buffer that always fits one line.
    constexpr size_t   kMinFrame = kFrameHead + kWireRecordHead + kMaxLine;

    struct Ring {
      uint8_t* data = nullptr;
      size_t   cap = 0;
      size_t   head = 0;       // next write offset
      size_t   tail = 0;       // offset of the oldest record
      size_t   used = 0;       // bytes held by whole records
      uint32_t firstSeq = 1;   // sequence of the oldest record
      uint32_t nextSeq = 1;    // sequence the next record gets
      uint32_t boot = 0;       // random per boot, lets clients detect a reboot
      uint32_t gen = 0;        // bumped when reserve() moves the records
    };

    inline Ring g_ring;
    inline probot::core::Mux g_mux;
    inline volatile bool g_resend = false;
    inline volatile bool g_noMemory = false;   // the lazy allocation failed: stop retrying

    // One long-line formatting buffer shared by all tasks; a task that
    // finds it busy logs the short (truncated) form instead.
    inline char g_scratch[kMaxLine + 1];
    inline volatile uint32_t g_scratchBusy = 0;

    inline void put(Ring& r, size_t off, const void* src, size_t n) {
      const uint8_t* s = static_cast<const uint8_t*>(src);
      size_t first = r.cap - off < n ? r.cap - off : n;
      memcpy(r.data + off, s, first);
      if (n > first) memcpy(r.data, s + first, n - first);
    }
    inline void get(const Ring& r, size_t off, void* dst, size_t n) {
      uint8_t* d = static_cast<uint8_t*>(dst);
      size_t first = r.cap - off < n ? r.cap - off : n;
      memcpy(d, r.data + off, first);
      if (n > first) memcpy(d + first, r.data, n - first);
    }
    inline size_t advance(const Ring& r, size_t off, size_t n) { return (off + n) % r.cap; }

    inline size_t recordSize(const Ring& r, size_t off) {
      uint8_t h[kRecordHead];
      get(r, off, h, sizeof h);
      return kRecordHead + (size_t)(h[5] | (h[6] << 8));
    }

    // Caller holds g_mux. len <= kMaxLine and the ring holds >= 2 lines.
    inline void pushLocked(Ring& r, Level lv, uint32_t ms, const char* text, size_t len) {
      const size_t need = kRecordHead + len;
      while (r.cap - r.used < need) {          // overwrite the oldest lines
        size_t sz = recordSize(r, r.tail);
        r.tail = advance(r, r.tail, sz);
        r.used -= sz;
        r.firstSeq++;
      }
      uint8_t h[kRecordHead] = {
        (uint8_t)lv, (uint8_t)ms, (uint8_t)(ms >> 8), (uint8_t)(ms >> 16), (uint8_t)(ms >> 24),
        (uint8_t)len, (uint8_t)(len >> 8) };
      put(r, r.head, h, sizeof h);
      put(r, advance(r, r.head, sizeof h), text, len);
      r.head = advance(r, r.head, need);
      r.used += need;
      r.nextSeq++;
    }

    inline uint32_t newBootId() {
      uint32_t id = esp_random();
      return id ? id : 1;
    }

    // Allocates (or resizes) the ring to `bytes`, keeping the newest lines
    // that fit. Allocation happens outside the critical section; inside, the
    // kept records are copied as one block to the start of the new ring.
    inline bool reserve(size_t bytes) {
      if (bytes < kMinBytes || bytes > kMaxBytes) return false;
      uint8_t* mem = (uint8_t*)malloc(bytes);
      if (!mem) return false;
      const uint32_t boot = newBootId();
      uint8_t* old;
      {
        probot::core::MuxGuard guard(g_mux);
        Ring& r = g_ring;
        if (r.data && r.cap == bytes) { old = mem; }
        else {
          size_t off = r.tail, used = r.used;
          uint32_t first = r.firstSeq;
          while (used > bytes) {                // drop the oldest that do not fit
            size_t sz = recordSize(r, off);
            off = advance(r, off, sz);
            used -= sz;
            first++;
          }
          if (used) get(r, off, mem, used);
          Ring fresh;
          fresh.data = mem;
          fresh.cap = bytes;
          fresh.head = used % bytes;
          fresh.used = used;
          fresh.firstSeq = first;
          fresh.nextSeq = r.nextSeq;
          fresh.boot = r.boot ? r.boot : boot;
          fresh.gen = r.gen + 1;
          old = r.data;
          r = fresh;
        }
      }
      free(old);
      return true;
    }

    inline void writeLine(Level lv, const char* text, size_t len) {
      if (!g_ring.data) {                       // no heap work per line once this failed
        if (g_noMemory) return;
        if (!reserve(kDefaultBytes)) { g_noMemory = true; return; }
      }
      uint32_t ms = (uint32_t)millis();
      probot::core::MuxGuard guard(g_mux);
      pushLocked(g_ring, lv, ms, text, len);
    }

    // Where a reader is in the ring: the next sequence it wants and that
    // record's offset (valid while seq >= firstSeq and gen matches the
    // ring). seq 0 = from the oldest.
    struct Cursor { uint32_t seq = 0; size_t off = 0; uint32_t gen = 0; };

    // Builds one 'L' frame with the lines from `c` on, as many as fit in
    // `cap`; advances `c`. Returns 0 when there is nothing new. Frame:
    // 'L', u32 boot, u32 firstSeq (oldest line still held), then records
    // { u32 seq, u32 ms, u8 level, u16 len, text }, all little-endian.
    inline size_t buildFrame(Cursor& c, uint8_t* out, size_t cap) {
      if (cap < kMinFrame) return 0;
      probot::core::MuxGuard guard(g_mux);
      Ring& r = g_ring;
      if (!r.data) return 0;
      if (c.seq == 0 || c.gen != r.gen || (int32_t)(c.seq - r.firstSeq) < 0 || (int32_t)(r.nextSeq - c.seq) < 0) {
        c.seq = r.firstSeq; c.off = r.tail; c.gen = r.gen;
      }
      if (c.seq == r.nextSeq) return 0;
      auto le32 = [](uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); };
      out[0] = FRAME_LINES;
      le32(out + 1, r.boot);
      le32(out + 5, r.firstSeq);
      size_t n = kFrameHead;
      while (c.seq != r.nextSeq) {
        uint8_t h[kRecordHead];
        get(r, c.off, h, sizeof h);
        size_t len = (size_t)(h[5] | (h[6] << 8));
        if (n + kWireRecordHead + len > cap) break;
        le32(out + n, c.seq);
        memcpy(out + n + 4, h + 1, 4);        // ms
        out[n + 8] = h[0];                     // level
        out[n + 9] = h[5]; out[n + 10] = h[6]; // len
        get(r, advance(r, c.off, kRecordHead), out + n + kWireRecordHead, len);
        n += kWireRecordHead + len;
        c.off = advance(r, c.off, kRecordHead + len);
        c.seq++;
      }
      return n;
    }

    // A client asked for the whole console again ('Q' resync).
    inline void requestResend() { __atomic_store_n(&g_resend, true, __ATOMIC_SEQ_CST); }
    inline bool takeResend() { return __atomic_exchange_n(&g_resend, false, __ATOMIC_SEQ_CST); }

    // Length of s[0, n) without a UTF-8 character cut in half at the end.
    inline size_t utf8Cut(const char* s, size_t n) {
      size_t i = n, cont = 0;
      while (i > 0 && cont < 3 && ((uint8_t)s[i - 1] & 0xC0) == 0x80) { i--; cont++; }
      if (i == 0) return n;
      uint8_t lead = (uint8_t)s[i - 1];
      size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
      return want > cont + 1 ? i - 1 : n;
    }

    inline void vformat(Level lv, const char* fmt, va_list ap);
  } // namespace detail

  // One console entry per call. A trailing newline is dropped; text longer
  // than 512 bytes becomes several lines (split at a newline when possible,
  // never inside a UTF-8 character).
  inline void write(Level lv, const char* text, size_t len) {
    if (!text) return;
    if (len && text[len - 1] == '\n') len--;
    do {
      size_t n = len;
      if (n > detail::kMaxLine) {
        n = detail::utf8Cut(text, detail::kMaxLine);
        for (size_t i = detail::kMaxLine; i > detail::kMaxLine / 2; --i)
          if (text[i - 1] == '\n') { n = i; break; }
      }
      size_t shown = (n && text[n - 1] == '\n') ? n - 1 : n;
      detail::writeLine(lv, text, shown);
      text += n;
      len -= n;
    } while (len);
  }
  inline void print(Level lv, const char* text) { if (text) write(lv, text, strlen(text)); }

  inline void info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  inline void warning(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  inline void error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  inline void info(const char* fmt, ...)    { va_list ap; va_start(ap, fmt); detail::vformat(Level::Info, fmt, ap); va_end(ap); }
  inline void warning(const char* fmt, ...) { va_list ap; va_start(ap, fmt); detail::vformat(Level::Warning, fmt, ap); va_end(ap); }
  inline void error(const char* fmt, ...)   { va_list ap; va_start(ap, fmt); detail::vformat(Level::Error, fmt, ap); va_end(ap); }

  namespace detail {
    // Short lines format on the stack; a longer one formats again into the
    // shared scratch buffer, up to kMaxLine bytes.
    inline void vformat(Level lv, const char* fmt, va_list ap) {
      char small[160];
      va_list again;
      va_copy(again, ap);
      int n = vsnprintf(small, sizeof small, fmt, ap);
      if (n >= 0 && (size_t)n < sizeof small) write(lv, small, (size_t)n);
      else if (n >= 0 && __atomic_exchange_n(&g_scratchBusy, 1u, __ATOMIC_ACQUIRE) == 0) {
        int m = vsnprintf(g_scratch, sizeof g_scratch, fmt, again);
        if (m > 0) write(lv, g_scratch, (size_t)m <= kMaxLine ? (size_t)m : utf8Cut(g_scratch, kMaxLine));
        __atomic_store_n(&g_scratchBusy, 0u, __ATOMIC_RELEASE);
      } else if (n >= 0) {
        write(lv, small, utf8Cut(small, sizeof small - 1));
      }
      va_end(again);
    }
  } // namespace detail

} // namespace probot::console
