#pragma once
#include <stdint.h>

// Fixed-rate cadence of the user task: initLoop/loop start every period
// regardless of how long the previous pass took. Config-free: a period set
// here overrides USER_LOOP_PERIOD_MS at runtime.

namespace probot {
  namespace core {
    constexpr uint32_t kMinLoopPeriodMs = 1, kMaxLoopPeriodMs = 1000;

    inline volatile uint32_t g_loop_period_ms = 0;           // 0 = default
    inline volatile uint32_t g_loop_period_default_ms = 20;  // set by runtime_setup()
    inline volatile uint32_t g_loop_overruns = 0;

    // Fixed-rate schedule in ticks (vTaskDelayUntil semantics). Pure logic
    // so host tests can drive it; the user task applies the result.
    class LoopPacer {
    public:
      void start(uint32_t now) { _last = now; }

      // The cycle ended at `now`: returns the ticks to sleep, at least 1 so
      // the task always blocks until the next tick. A cycle that ended past
      // its deadline is an overrun; the schedule then restarts after the
      // sleep instead of running the missed periods back to back. A cycle
      // that ran a one-shot init/start/stop hook (`oneShot`) is late by
      // design and not counted.
      uint32_t afterCycle(uint32_t now, uint32_t period, bool oneShot, bool* overrun) {
        uint32_t next = _last + period;
        int32_t left = (int32_t)(next - now);
        *overrun = left < 0 && !oneShot;
        if (left >= 0) { _last = next; return left ? (uint32_t)left : 1u; }
        _last = now + 1;
        return 1;
      }

    private:
      uint32_t _last = 0;
    };

    // Heartbeat stamp for the sleep after a cycle: the wake time, so the
    // stall check never counts the sleep (a period may exceed
    // PROBOT_LOOP_DEADLINE_MS). Never 0, which means "no heartbeat".
    inline uint32_t wakeStampMs(uint32_t nowMs, uint32_t sleepTicks, uint32_t msPerTick) {
      uint32_t wake = nowMs + sleepTicks * msPerTick;
      return wake ? wake : 1u;
    }
  } // namespace core

  // Sets the initLoop/loop period in ms (1-1000), effective from the next
  // cycle; 0 restores USER_LOOP_PERIOD_MS. Returns false for other values.
  inline bool setLoopPeriodMs(uint32_t ms) {
    if (ms != 0 && (ms < core::kMinLoopPeriodMs || ms > core::kMaxLoopPeriodMs)) return false;
    __atomic_store_n(&core::g_loop_period_ms, ms, __ATOMIC_SEQ_CST);
    return true;
  }

  inline uint32_t loopPeriodMs() {
    uint32_t ms = __atomic_load_n(&core::g_loop_period_ms, __ATOMIC_SEQ_CST);
    return ms ? ms : __atomic_load_n(&core::g_loop_period_default_ms, __ATOMIC_SEQ_CST);
  }

  // User task cycles since boot that ended after their deadline.
  inline uint32_t loopOverruns() {
    return __atomic_load_n(&core::g_loop_overruns, __ATOMIC_SEQ_CST);
  }

} // namespace probot
