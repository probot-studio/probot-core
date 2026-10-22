#pragma once
#include <new>
#include <probot/io/gamepad.hpp>

// Fresh gamepad services (no frames, no names) so assertions about
// "never received" do not depend on the order test files run in.
inline void resetGamepadsForTest(uint32_t timeoutMs = 500) {
  using probot::io::GamepadService;
  namespace d = probot::io::detail;
  for (uint8_t p = 0; p < probot::io::kMaxGamepads; ++p) {
    d::g_gamepads[p].~GamepadService();
    new (&d::g_gamepads[p]) GamepadService();
    d::g_raw_gamepads[p].~GamepadService();
    new (&d::g_raw_gamepads[p]) GamepadService();
  }
  d::setTimeoutMs(timeoutMs);
}
