#pragma once
#include <stdint.h>
#include <probot/core/lifecycle.hpp>
#include <probot/robot/state.hpp>

// Pure status-LED policy, shared by the sysloop and host tests. `blinkOn` is
// the current half of the 1 Hz blink cycle.
namespace probot::core {

  struct Rgb { uint8_t r, g, b; };

  inline bool operator==(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

  inline Rgb statusColor(const robot::StateSnapshot& s, bool estop, bool blinkOn) {
    constexpr Rgb off{0, 0, 0};
    if (estop) return {255, 0, 0};                         // solid red: terminal
    if (s.deadlineMiss) return blinkOn ? Rgb{255, 0, 0} : off; // blinking red: stalled
    switch (s.phase) {
      case robot::Phase::STOPPED:
        // Solid blue: no Driver Station. Blinking blue: connected, stopped.
        if (s.clientCount <= 0) return {0, 0, 255};
        return blinkOn ? Rgb{0, 0, 255} : off;
      case robot::Phase::AUTO_INIT:
      case robot::Phase::TELEOP_INIT:
        return {255, 255, 0};                              // solid yellow
      case robot::Phase::TRANSITION:
        return blinkOn ? Rgb{255, 255, 0} : off;           // blinking yellow
      case robot::Phase::AUTO_RUN:
        return blinkOn ? Rgb{255, 128, 0} : off;           // blinking orange
      case robot::Phase::TELEOP_RUN:
        return blinkOn ? Rgb{0, 255, 0} : off;             // blinking green
    }
    return off;
  }

  // Robot signal light: blinks only while the robot may move.
  inline bool rslBlinking(robot::Phase phase, bool estop) {
    return !estop && isRunPhase(phase);
  }

} // namespace probot::core
