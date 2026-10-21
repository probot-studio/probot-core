#pragma once
#include <stdint.h>
#include <Arduino.h>
#include <probot/core/lifecycle.hpp>
#include <probot/io/gamepad.hpp>
#include <probot/robot/opmodes.hpp>
#include <probot/robot/state.hpp>

// Typed, read-only view of the robot and Driver Station state.
// Config-free: safe to include from any translation unit.

namespace probot::ds {

  using robot::OpMode;
  using robot::OpModeOption;
  using robot::Phase;

  // Latched emergency stop (terminal until reboot).
  inline bool isEStopped() {
    return __atomic_load_n(&robot::g_estop_latched, __ATOMIC_SEQ_CST) != 0;
  }

  // What is running. STOPPED once the emergency stop has latched (the
  // published phase is not updated after the user task is killed).
  inline Phase phase() {
    return isEStopped() ? Phase::STOPPED : robot::state().phase();
  }

  inline OpMode selectedMode() {
    return robot::state().selectedMode();
  }

  // The OpMode option picked in the Driver Station (an id from
  // setOpModeOptions()), or 0 when none is listed for the selected mode.
  // It may change while a run is still ending (autonomous expiry
  // preselects TeleOp); hooks use activeOpModeId().
  inline uint64_t selectedOpModeId() {
    return robot::state().opModeId();
  }

  // The option the current (or last) run was INITed with; 0 before the
  // first INIT or when none was selected.
  inline uint64_t activeOpModeId() {
    return robot::state().activeOpModeId();
  }

  // UTILITY can be selected: the sketch defines utilityLoop() or a utility
  // option is listed.
  inline bool utilityAvailable() {
    return __atomic_load_n(&robot::g_utility_hooks, __ATOMIC_SEQ_CST) != 0 ||
           robot::opModes().hasMode(OpMode::UTILITY);
  }

  namespace detail {
    // While idle, moves a selection that is no longer offered to the
    // preferred option of its mode, or to TeleOp when UTILITY is gone.
    inline void reconcileSelection() {
      auto& reg = robot::opModes();
      auto s = robot::state().read();
      OpMode mode = s.selectedMode;
      if (mode == OpMode::UTILITY && !utilityAvailable()) mode = OpMode::TELEOP;
      OpMode listed;
      if (mode == s.selectedMode && s.opModeId && reg.find(s.opModeId, &listed) &&
          listed == mode) return;
      uint64_t next = reg.preferred(mode);
      if (mode == s.selectedMode && next == s.opModeId) return;
      robot::state().replaceSelectionIfIdle((uint32_t)millis(), s.selectedMode, s.opModeId,
                                            mode, next);
    }
  } // namespace detail

  // Replaces the OpMode options the Driver Station offers (at most 64; the
  // strings are copied). Returns false and keeps the old list when an option
  // is invalid. While the robot is idle, a selection that is no longer
  // listed moves to the option preferred() picks; otherwise INIT corrects it.
  inline bool setOpModeOptions(const OpModeOption* options, size_t count) {
    if (!robot::opModes().set(options, count)) return false;
    detail::reconcileSelection();
    return true;
  }

  // Actuators may move: started RUN phase, not stalled, not e-stopped.
  // Same rule that gates the joystick input user code receives.
  inline bool outputsEnabled() {
    return !isEStopped() && core::inputsEnabled(robot::state().read());
  }

  // A Driver Station client currently owns the robot.
  inline bool isAttached() {
    return robot::state().read().clientCount > 0;
  }

  // Remaining time of the current timed period in seconds; negative when
  // there is none. Only the autonomous period is timed today, so this is
  // >= 0 in AUTO_RUN and -1 otherwise.
  inline float matchTimeSeconds() {
    if (isEStopped()) return -1.0f;
    auto s = robot::state().read();
    if (s.phase != Phase::AUTO_RUN || s.autoStartMs == 0 || s.autoPeriodSeconds <= 0) return -1.0f;
    int32_t remainingMs = (int32_t)(s.autoStartMs + (uint32_t)s.autoPeriodSeconds * 1000u - (uint32_t)millis());
    return remainingMs > 0 ? (float)remainingMs / 1000.0f : 0.0f;
  }

  // Increments on every DS joystick frame and every accepted mode, OpMode,
  // INIT, START or STOP command (not on e-stop or reboot requests).
  inline uint32_t packetSeq() {
    return __atomic_load_n(&robot::g_ds_packet_seq, __ATOMIC_SEQ_CST);
  }

  // Ungated controller input in any phase (see io::rawGamepad()). Returns
  // false when the port does not exist or no frame was ever received.
  // Only port 0 exists today.
  inline bool rawGamepad(int port, io::GamepadSnapshot& out) {
    if (port != 0) return false;
    out = io::rawGamepad().read();
    return out.seq != 0;
  }

} // namespace probot::ds
