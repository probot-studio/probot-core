#pragma once
#include <stdint.h>
#include <probot/core/lock.hpp>

namespace probot::robot {
  enum class Status : uint8_t { INIT=0, START=1, STOP=2 };
  enum class Phase  : uint8_t {
    STOPPED=0,
    AUTO_INIT=1,
    AUTO_RUN=2,
    TELEOP_INIT=3,
    TELEOP_RUN=4,
    TRANSITION=5
  };
  enum class OpMode : uint8_t { AUTO=0, TELEOP=1 };

  struct StateSnapshot {
    uint32_t ms;
    uint32_t seq;
    Status   status;
    Phase    phase;
    OpMode   selectedMode;
    float    batteryVoltage;
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
      s.ms=0; s.seq=0; s.status=Status::STOP; s.phase=Phase::STOPPED;
      s.selectedMode=OpMode::TELEOP; s.batteryVoltage=0.0f;
      s.autoPeriodSeconds=30; s.autoStartMs=0; s.clientCount=0; s.deadlineMiss=false;
      _buf[0] = s; _buf[1] = s;
    }

    void setStatus(uint32_t now_ms, Status st){ writeField(now_ms, [&](StateSnapshot& w){ w.status = st; }); }
    void setPhase(uint32_t now_ms, Phase ph){ writeField(now_ms, [&](StateSnapshot& w){ w.phase = ph; }); }
    void setSelectedMode(uint32_t now_ms, OpMode mode){ writeField(now_ms, [&](StateSnapshot& w){ w.selectedMode = mode; }); }
    void setControl(uint32_t now_ms, Status st, OpMode mode){
      writeField(now_ms, [&](StateSnapshot& w){ w.status = st; w.selectedMode = mode; });
    }
    void setBatteryVoltage(uint32_t now_ms, float v){ writeField(now_ms, [&](StateSnapshot& w){ w.batteryVoltage = v; }); }
    void setAutoPeriodSeconds(uint32_t now_ms, int32_t s){ writeField(now_ms, [&](StateSnapshot& w){ w.autoPeriodSeconds = s; }); }
    void setAutoStartMs(uint32_t now_ms, uint32_t ms){ writeField(now_ms, [&](StateSnapshot& w){ w.autoStartMs = ms; }); }
    void setClientCount(uint32_t now_ms, int32_t c){ writeField(now_ms, [&](StateSnapshot& w){ w.clientCount = c; }); }
    void setDeadlineMiss(uint32_t now_ms, bool v){ writeField(now_ms, [&](StateSnapshot& w){ w.deadlineMiss = v; }); }

    // Readers take the same short critical section as writers: a
    // double buffer alone cannot protect a reader that gets preempted
    // mid-copy while the writer flips twice.
    StateSnapshot read() const {
      probot::core::MuxGuard guard(_mux);
      return _buf[_cur];
    }

    // Convenience getters
    int32_t  autoPeriodSeconds() const { return read().autoPeriodSeconds; }
    uint32_t autoStartMs() const       { return read().autoStartMs; }
    Status   status() const            { return read().status; }
    Phase    phase() const             { return read().phase; }
    OpMode   selectedMode() const      { return read().selectedMode; }

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

  // User-code heartbeat — updated after initLoop/loop iterations.
  // Checked by health endpoint and sysloop to detect blocked user code.
  inline volatile uint32_t g_loop_heartbeat_ms = 0;

  // Driver station activity — updated by HTTP/WS handlers on every request.
  // Checked by sysloop to detect connection loss.
  inline volatile uint32_t g_ds_last_activity_ms = 0;

  // Incremented on every Driver Station joystick frame and accepted mode,
  // INIT, START or STOP command; lets polling code detect "new DS data"
  // without comparing values.
  inline volatile uint32_t g_ds_packet_seq = 0;
  inline void noteDsPacket(){
    __atomic_add_fetch(&g_ds_packet_seq, 1u, __ATOMIC_SEQ_CST);
  }

  // Emergency stop. Set requested=1 from an HTTP handler; the sysloop runs
  // the terminal emergency-stop sequence and sets latched=1. While latched,
  // init/start commands are refused — the robot stays dead until reboot.
  inline volatile uint32_t g_estop_requested = 0;
  inline volatile uint32_t g_estop_latched   = 0;
  // Deliberate software reboot request (the "reboot to clear estop" button).
  inline volatile uint32_t g_reboot_requested = 0;
}

namespace probot {
  // Request a terminal emergency stop. Safe to call from ANY task, including
  // a user hook: it only sets a flag. The sysloop runs the real sequence
  // (kill the user task, run the active stop under a watchdog, latch),
  // so it never deletes its own caller.
  inline void emergencyStop(){
    __atomic_store_n(&probot::robot::g_estop_requested, 1u, __ATOMIC_SEQ_CST);
  }
}
