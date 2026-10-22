#pragma once
#include <stddef.h>
#include <stdint.h>
#include <probot/robot/state.hpp>
#ifdef ESP32
#include <sys/time.h>
#endif

// Wall clock from the Driver Station. The robot has no RTC battery, so the
// page sends the browser's UTC time when it connects and the robot sets its
// system clock (settimeofday) from it. Data logs then get real timestamps
// and file names; time() and gettimeofday() work as usual afterwards.
// The time is UTC: localtime() and strftime() give UTC unless the sketch
// sets a zone (setenv("TZ", "<+03>-3", 1); tzset();). The clock is only as
// right as the owner's device.

namespace probot::wallclock {

  namespace detail {
    constexpr uint8_t kFrame = 0x55;                  // 'U'
    constexpr size_t  kFrameSize = 9;                 // 'U', i64 Unix ms (LE)
    constexpr int64_t kMinUnixMs = 1704067200000LL;   // 2024-01-01T00:00:00Z
    constexpr int64_t kMaxUnixMs = 4102444800000LL;   // 2100-01-01T00:00:00Z

    inline volatile bool g_set = false;

    // Validates a 'U' frame from the network.
    inline bool decodeFrame(const uint8_t* data, size_t len, int64_t& unixMs) {
      if (!data || len != kFrameSize || data[0] != kFrame) return false;
      uint64_t v = 0;
      for (int i = 8; i >= 1; --i) v = (v << 8) | data[i];
      unixMs = (int64_t)v;
      return unixMs >= kMinUnixMs && unixMs < kMaxUnixMs;
    }

    // A clock already set may move only while no match is under way:
    // stopped with nothing pending. TRANSITION (between autonomous and
    // TeleOp) and every INIT or RUN phase keep the log's timeline.
    inline bool mayReset(robot::Phase phase, robot::Status status) {
      return phase == robot::Phase::STOPPED && status == robot::Status::STOP;
    }

    // Unix ms (in range, so non-negative) as seconds and microseconds.
    inline void split(int64_t unixMs, int64_t& sec, int32_t& usec) {
      sec = unixMs / 1000;
      usec = (int32_t)(unixMs % 1000) * 1000;
    }

    // Applies a Driver Station time: the first valid one always, later ones
    // only when mayReset(). Returns whether the clock was set.
    inline bool setFromDs(int64_t unixMs, robot::Phase phase, robot::Status status) {
      if (unixMs < kMinUnixMs || unixMs >= kMaxUnixMs) return false;
      if (__atomic_load_n(&g_set, __ATOMIC_SEQ_CST) && !mayReset(phase, status)) return false;
      int64_t sec;
      int32_t usec;
      split(unixMs, sec, usec);
#ifdef ESP32
      struct timeval tv;
      tv.tv_sec = (time_t)sec;
      tv.tv_usec = (suseconds_t)usec;
      if (settimeofday(&tv, nullptr) != 0) return false;
#endif
      __atomic_store_n(&g_set, true, __ATOMIC_SEQ_CST);
      return true;
    }
  } // namespace detail

  // The system clock has been set from a Driver Station since boot. Before
  // that, time() counts from 1970 after power-on; after a software restart
  // (reboot request, panic, watchdog) the ESP32-S3 keeps the boot time
  // offset in RTC memory, so time() may continue an older real time, which
  // still does not count as set.
  inline bool isSet() { return __atomic_load_n(&detail::g_set, __ATOMIC_SEQ_CST); }

} // namespace probot::wallclock
