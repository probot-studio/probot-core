#pragma once
#include <stdint.h>
#include <probot/robot/state.hpp>

// A single initLoop/loop iteration that has not returned within this many ms
// is treated as STALLED: inputs are zeroed and the robot is held safe
// (no kill, no reboot). Must stay below the hardware watchdog timeout.
#ifndef PROBOT_LOOP_DEADLINE_MS
#define PROBOT_LOOP_DEADLINE_MS 2000
#endif

// The same stall signal for one-shot init/start/stop hooks, with a longer
// budget: they may block legitimately (e.g. sensor calibration in init()),
// but a hook that never returns must still show up as PB-E301.
#ifndef PROBOT_HOOK_DEADLINE_MS
#define PROBOT_HOOK_DEADLINE_MS 10000
#endif

static_assert((PROBOT_HOOK_DEADLINE_MS) >= (PROBOT_LOOP_DEADLINE_MS),
  "PROBOT_HOOK_DEADLINE_MS must be >= PROBOT_LOOP_DEADLINE_MS");

// Cooperative FTC-style OpMode lifecycle core. This file is pure logic:
// FreeRTOS plumbing, watchdogs and the terminal emergency stop live in
// runtime.hpp, while host tests exercise the state machine here.
//
// There is one persistent user task. The supervisor publishes only a desired
// OpMode/stage; this machine observes it at a user-code boundary and invokes
// every hook itself. Normal stop/phase changes therefore never interrupt a
// hook while it owns a bus or allocator lock.

namespace probot::core {

  using OpMode = robot::OpMode;
  enum class Stage : uint8_t { STOPPED=0, INIT=1, RUN=2, TRANSITION=3 };

  struct DesiredState {
    OpMode mode;
    Stage stage;
  };

  inline bool operator==(DesiredState a, DesiredState b) {
    return a.mode == b.mode && a.stage == b.stage;
  }
  inline bool operator!=(DesiredState a, DesiredState b) { return !(a == b); }

  // Optional hooks may be null; the four loop/stop hooks are required by the
  // public API and runtime always supplies them.
  struct Hooks {
    void (*autonomousInit)();
    void (*autonomousInitLoop)();
    void (*autonomousStart)();
    void (*autonomousLoop)();
    void (*autonomousStop)();
    void (*teleopInit)();
    void (*teleopInitLoop)();
    void (*teleopStart)();
    void (*teleopLoop)();
    void (*teleopStop)();
  };

  inline robot::Phase phaseFor(OpMode mode, Stage stage) {
    if (stage == Stage::TRANSITION) return robot::Phase::TRANSITION;
    if (stage == Stage::STOPPED) return robot::Phase::STOPPED;
    if (mode == OpMode::AUTO) {
      return stage == Stage::INIT ? robot::Phase::AUTO_INIT : robot::Phase::AUTO_RUN;
    }
    return stage == Stage::INIT ? robot::Phase::TELEOP_INIT : robot::Phase::TELEOP_RUN;
  }

  inline bool isInitPhase(robot::Phase phase) {
    return phase == robot::Phase::AUTO_INIT || phase == robot::Phase::TELEOP_INIT;
  }
  inline bool isRunPhase(robot::Phase phase) {
    return phase == robot::Phase::AUTO_RUN || phase == robot::Phase::TELEOP_RUN;
  }
  inline bool canSelectMode(robot::Phase phase) {
    return phase == robot::Phase::STOPPED || phase == robot::Phase::TRANSITION;
  }

  // Command acceptance — the single source of truth shared by the HTTP
  // handler and host tests. Status closes the supervisor/user-task gap:
  // after a command is accepted the public phase lags until the hook
  // returns, so phase alone would accept duplicates (e.g. a second START
  // while start() is still running).
  inline bool canAcceptModeChange(robot::Phase phase, robot::Status status) {
    return canSelectMode(phase) && status == robot::Status::STOP;
  }
  inline bool canAcceptInit(robot::Phase phase, robot::Status status) {
    return canSelectMode(phase) && status == robot::Status::STOP;
  }
  inline bool canAcceptStart(robot::Phase phase, robot::Status status) {
    return isInitPhase(phase) && status == robot::Status::INIT;
  }
  inline bool canAcceptStop(robot::Phase phase) {
    return isInitPhase(phase) || isRunPhase(phase);
  }

  class PhaseMachine {
  public:
    OpMode currentMode() const { return cur_.mode; }
    Stage currentStage() const { return cur_.stage; }
    DesiredState current() const { return cur_; }

    // One user-task iteration. `neutralize` is called throughout every
    // non-RUN stage, not only on entry, so INIT input remains neutral even if
    // the Driver Station keeps sending controller frames. The heartbeat is
    // primed immediately before the recurring initLoop/loop hook.
    void step(const Hooks& h, DesiredState requested, robot::StateService& rs,
              uint32_t now, void (*neutralize)() = nullptr,
              volatile uint32_t* hb = nullptr,
              uint32_t (*clockNow)() = nullptr) {
      if (requested != cur_) transition(h, requested, rs, now, neutralize, hb, clockNow);

      if (cur_.stage != Stage::RUN && neutralize) neutralize();
      uint32_t hookStartedAt = timeNow(now, clockNow);
      if (cur_.stage == Stage::INIT) {
        primeHeartbeat(hookStartedAt, hb);
        callInitLoop(h, cur_.mode);
      } else if (cur_.stage == Stage::RUN) {
        primeHeartbeat(hookStartedAt, hb);
        callLoop(h, cur_.mode);
      }
    }

  private:
    static void primeHeartbeat(uint32_t now, volatile uint32_t* hb) {
      if (!hb) return;
      *hb = now == 0 ? 1u : now;
    }
    // One-shot hooks (init/start/stop) get PROBOT_HOOK_DEADLINE_MS instead
    // of the loop deadline. The supervisor compares against the loop
    // deadline only, so the heartbeat is stamped that much into the future.
    static void primeOneShotHeartbeat(uint32_t now, volatile uint32_t* hb) {
      primeHeartbeat(now + (uint32_t)(PROBOT_HOOK_DEADLINE_MS - PROBOT_LOOP_DEADLINE_MS), hb);
    }
    static uint32_t timeNow(uint32_t now, uint32_t (*clockNow)()) {
      return clockNow ? clockNow() : now;
    }

    static void callInit(const Hooks& h, OpMode mode) {
      auto fn = mode == OpMode::AUTO ? h.autonomousInit : h.teleopInit;
      if (fn) fn();
    }
    static void callInitLoop(const Hooks& h, OpMode mode) {
      auto fn = mode == OpMode::AUTO ? h.autonomousInitLoop : h.teleopInitLoop;
      if (fn) fn();
    }
    static void callStart(const Hooks& h, OpMode mode) {
      auto fn = mode == OpMode::AUTO ? h.autonomousStart : h.teleopStart;
      if (fn) fn();
    }
    static void callLoop(const Hooks& h, OpMode mode) {
      auto fn = mode == OpMode::AUTO ? h.autonomousLoop : h.teleopLoop;
      if (fn) fn();
    }
    static void callStop(const Hooks& h, OpMode mode) {
      auto fn = mode == OpMode::AUTO ? h.autonomousStop : h.teleopStop;
      if (fn) fn();
    }

    void publish(robot::StateService& rs, uint32_t now) {
      rs.setPhase(now, phaseFor(cur_.mode, cur_.stage));
    }

    void transition(const Hooks& h, DesiredState requested,
                    robot::StateService& rs, uint32_t now,
                    void (*neutralize)(), volatile uint32_t* hb,
                    uint32_t (*clockNow)()) {
      if (neutralize) neutralize();

      if (requested.stage == Stage::STOPPED || requested.stage == Stage::TRANSITION) {
        if (cur_.stage == Stage::INIT || cur_.stage == Stage::RUN) {
          primeOneShotHeartbeat(timeNow(now, clockNow), hb);
          callStop(h, cur_.mode);
        }
        cur_ = requested;
        publish(rs, now);
        return;
      }

      // A mode change while active is not a legal protocol transition. Be
      // conservative if an internal caller ever requests it: stop the old
      // OpMode and settle in STOPPED; do not enter the new one in this step.
      if (requested.mode != cur_.mode &&
          (cur_.stage == Stage::INIT || cur_.stage == Stage::RUN)) {
        primeOneShotHeartbeat(timeNow(now, clockNow), hb);
        callStop(h, cur_.mode);
        cur_ = { requested.mode, Stage::STOPPED };
        publish(rs, now);
        return;
      }

      if (requested.stage == Stage::INIT) {
        if (cur_.stage != Stage::STOPPED && cur_.stage != Stage::TRANSITION) return;
        primeOneShotHeartbeat(timeNow(now, clockNow), hb);
        cur_ = requested;
        publish(rs, now);
        callInit(h, cur_.mode);
        return;
      }

      // START is valid only after INIT of the same selected OpMode.
      if (requested.stage == Stage::RUN) {
        if (cur_.stage != Stage::INIT || cur_.mode != requested.mode) return;
        primeOneShotHeartbeat(timeNow(now, clockNow), hb);
        callStart(h, cur_.mode);
        uint32_t enteredRunAt = timeNow(now, clockNow);
        if (cur_.mode == OpMode::AUTO) rs.setAutoStartMs(enteredRunAt, enteredRunAt);
        cur_ = requested;
        publish(rs, enteredRunAt);
        primeHeartbeat(enteredRunAt, hb);
      }
    }

    DesiredState cur_{ OpMode::TELEOP, Stage::STOPPED };
  };

  // Supervisor-side policy. It never invokes hooks; it only translates the
  // requested command state and enforces the autonomous RUN duration.
  class Supervisor {
  public:
    DesiredState update(robot::StateService& rs, uint32_t now, bool estopLatched) {
      auto s = rs.read();
      if (estopLatched) return { s.selectedMode, Stage::STOPPED };

      // Count only a genuinely entered AUTO_RUN with a fresh non-zero stamp.
      if (s.status == robot::Status::START && s.selectedMode == OpMode::AUTO &&
          s.phase == robot::Phase::AUTO_RUN && s.autoStartMs != 0 &&
          s.autoPeriodSeconds > 0 &&
          (int32_t)(now - s.autoStartMs) >= s.autoPeriodSeconds * 1000) {
        rs.setControl(now, robot::Status::STOP, OpMode::TELEOP);
        return { OpMode::TELEOP, Stage::TRANSITION };
      }

      // Between the supervisor's expiry write and the user task's stop hook,
      // AUTO_RUN is still public. Preserve TRANSITION until stop has run.
      if (s.phase == robot::Phase::AUTO_RUN && s.status == robot::Status::STOP &&
          s.selectedMode == OpMode::TELEOP) {
        return { OpMode::TELEOP, Stage::TRANSITION };
      }
      if (s.phase == robot::Phase::TRANSITION && s.status == robot::Status::STOP) {
        return { s.selectedMode, Stage::TRANSITION };
      }

      Stage stage = Stage::STOPPED;
      if (s.status == robot::Status::INIT) stage = Stage::INIT;
      else if (s.status == robot::Status::START) stage = Stage::RUN;
      return { s.selectedMode, stage };
    }
  };

  inline bool isStalled(robot::Phase phase, uint32_t now, uint32_t heartbeat_ms,
                        uint32_t deadline_ms) {
    bool userLoopPhase = isInitPhase(phase) || isRunPhase(phase);
    return userLoopPhase && heartbeat_ms != 0 &&
           (int32_t)(now - heartbeat_ms) > (int32_t)deadline_ms;
  }

} // namespace probot::core
