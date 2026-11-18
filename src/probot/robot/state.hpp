#pragma once
#include <stdint.h>
#include <probot/core/lock.hpp>

namespace probot::robot {
  enum class Status : uint8_t { INIT=0, START=1, STOP=2 };
  enum class Phase  : uint8_t { NOT_INIT=0, INITED=1, AUTONOMOUS=2, TELEOP=3 };

  struct StateSnapshot {
    uint32_t ms;
    uint32_t seq;
    Status   status;
    Phase    phase;
    float    batteryVoltage;
    bool     autonomousEnabled;
    int32_t  autoPeriodSeconds;
    uint32_t autoStartMs;
    int32_t  clientCount;
    bool     deadlineMiss;
  };

  class StateService {
  public:
    StateService(){
      _cur = 0;
      StateSnapshot s{};
      s.ms=0; s.seq=0; s.status=Status::STOP; s.phase=Phase::NOT_INIT; s.batteryVoltage=0.0f;
      s.autonomousEnabled=false; s.autoPeriodSeconds=30; s.autoStartMs=0; s.clientCount=0; s.deadlineMiss=false;
      _buf[0] = s; _buf[1] = s;
    }

    void setStatus(uint32_t now_ms, Status st){ writeField(now_ms, [&](StateSnapshot& w){ w.status = st; }); }
    void setPhase(uint32_t now_ms, Phase ph){ writeField(now_ms, [&](StateSnapshot& w){ w.phase = ph; }); }
    void setBatteryVoltage(uint32_t now_ms, float v){ writeField(now_ms, [&](StateSnapshot& w){ w.batteryVoltage = v; }); }
    void setAutonomous(uint32_t now_ms, bool en){ writeField(now_ms, [&](StateSnapshot& w){ w.autonomousEnabled = en; }); }
    void setAutoPeriodSeconds(uint32_t now_ms, int32_t s){ writeField(now_ms, [&](StateSnapshot& w){ w.autoPeriodSeconds = s; }); }
    void setAutoStartMs(uint32_t now_ms, uint32_t ms){ writeField(now_ms, [&](StateSnapshot& w){ w.autoStartMs = ms; }); }
    void setClientCount(uint32_t now_ms, int32_t c){ writeField(now_ms, [&](StateSnapshot& w){ w.clientCount = c; }); }
    void setDeadlineMiss(uint32_t now_ms, bool v){ writeField(now_ms, [&](StateSnapshot& w){ w.deadlineMiss = v; }); }
    // Publish a non-snapshot service change (for example, a selected auto ID)
    // through the same sequence used by the Driver Station WS push task.
    void touch(uint32_t now_ms){ writeField(now_ms, [](StateSnapshot&){}); }

    // Readers take the same short critical section as writers: a
    // double buffer alone cannot protect a reader that gets preempted
    // mid-copy while the writer flips twice.
    StateSnapshot read() const {
      probot::core::MuxGuard guard(_mux);
      return _buf[_cur];
    }

    // Convenience getters
    bool     autonomousEnabled() const { return read().autonomousEnabled; }
    int32_t  autoPeriodSeconds() const { return read().autoPeriodSeconds; }
    uint32_t autoStartMs() const       { return read().autoStartMs; }
    Status   status() const            { return read().status; }
    Phase    phase() const             { return read().phase; }

  private:
    template<typename Fn>
    void writeField(uint32_t now_ms, Fn fn){
      probot::core::MuxGuard guard(_mux);
      uint32_t cur = _cur;
      uint32_t w   = 1u - cur;
      StateSnapshot s = _buf[cur];
      s.ms = now_ms;
      s.seq++;
      fn(s);
      _buf[w] = s;
      _cur = w;
    }

    mutable StateSnapshot     _buf[2];
    mutable volatile uint32_t _cur;
    probot::core::Mux         _mux;
  };

  inline StateService& state(){
    static StateService instance;
    return instance;
  }

  // User loop heartbeat — updated by teleop/auto workers each iteration.
  // Checked by health endpoint and sysloop to detect blocked code.
  inline volatile uint32_t g_loop_heartbeat_ms = 0;

  // Driver station activity — updated by HTTP/WS handlers on every request.
  // Checked by sysloop to detect connection loss.
  inline volatile uint32_t g_ds_last_activity_ms = 0;

  // Emergency stop. Set requested=1 from an HTTP handler; the sysloop runs
  // the terminal emergency-stop sequence and sets latched=1. While latched,
  // init/start commands are refused — the robot stays dead until reboot.
  inline volatile uint32_t g_estop_requested = 0;
  inline volatile uint32_t g_estop_latched   = 0;
  // Deliberate software reboot request (the "reboot to clear estop" button).
  inline volatile uint32_t g_reboot_requested = 0;
}
