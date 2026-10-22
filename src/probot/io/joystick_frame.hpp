#pragma once
#include <stddef.h>
#include <stdint.h>
#include <probot/io/gamepad.hpp>

// Decoding of the Driver Station's 'J' (joystick) and 'N' (gamepad info)
// WebSocket frames. Pure logic, so host tests cover the validation of this
// network input; the layouts are documented in ws_joystick.hpp.

namespace probot::io::frame {

  constexpr uint8_t  kJoystick = 0x4A;   // 'J'
  constexpr uint8_t  kInfo     = 0x4E;   // 'N'
  constexpr uint32_t kMaxAxes = 20, kMaxButtons = 20;
  // Largest 'J': header, axes, button bits, value count and values.
  constexpr size_t kMaxSize = 4 + kMaxAxes * 2 + (kMaxButtons + 7) / 8 + 1 + kMaxButtons;
  static_assert(kMaxSize >= 4 + kMaxGamepadName, "an 'N' frame must fit kMaxSize");

  struct Joystick {
    uint8_t  port;
    uint32_t axisCount, buttonCount, valueCount;
    float    axes[kMaxAxes];
    bool     buttons[kMaxButtons];
    uint8_t  values[kMaxButtons];
  };

  // False for anything malformed. Counts beyond the protocol limit reject
  // rather than clamp: the sender laid out buttons after axisCount * 2
  // bytes, so clamping would read axis bytes as button bits.
  inline bool decodeJoystick(const uint8_t* d, size_t len, Joystick& out) {
    if (len < 4 || d[0] != kJoystick) return false;
    const uint32_t nA = d[1], nB = d[2];
    if (nA > kMaxAxes || nB > kMaxButtons || d[3] >= kMaxGamepads) return false;
    const size_t base = 4 + nA * 2 + (nB + 7) / 8;
    if (len < base) return false;
    uint32_t nV = 0;
    if (len > base) {                       // optional analog values
      nV = d[base];
      if (nV > nB || len != base + 1 + nV) return false;
    }
    out.port = d[3];
    out.axisCount = nA; out.buttonCount = nB; out.valueCount = nV;
    const uint8_t* p = d + 4;
    for (uint32_t i = 0; i < nA; i++, p += 2) {
      int16_t raw = static_cast<int16_t>((p[0] << 8) | p[1]);
      out.axes[i] = static_cast<float>(raw) / 32767.0f;
    }
    for (uint32_t i = 0; i < nB; i++) out.buttons[i] = (p[i / 8] >> (i % 8)) & 1;
    for (uint32_t i = 0; i < nV; i++) out.values[i] = d[base + 1 + i];
    return true;
  }

  struct Info {
    uint8_t     port;
    bool        standard;   // W3C standard mapping
    const char* name;       // points into the frame, not terminated
    size_t      nameLen;
  };

  inline bool decodeInfo(const uint8_t* d, size_t len, Info& out) {
    if (len < 4 || d[0] != kInfo || d[1] >= kMaxGamepads) return false;
    const size_t n = d[3];
    if (n > kMaxGamepadName || len != 4 + n) return false;
    out.port = d[1];
    out.standard = (d[2] & 1) != 0;
    out.name = (const char*)d + 4;
    out.nameLen = n;
    return true;
  }

} // namespace probot::io::frame
