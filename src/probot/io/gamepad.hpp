#pragma once
#include <stdint.h>
#include <string.h>
#include <Arduino.h>
#include <probot/core/lock.hpp>

// How long after the last received joystick packet axes/buttons keep
// their values before read() returns neutral. FRC uses 125 ms, FTC
// ~300 ms, WPILib XRP 500 ms; tighten for faster failsafe.
#ifndef PROBOT_INPUT_TIMEOUT_MS
#define PROBOT_INPUT_TIMEOUT_MS 500
#endif

namespace probot::io {
  // Driver Station ports, as in WPILib: 0 is the driver's gamepad.
  constexpr uint8_t kMaxGamepads = 6;
  constexpr size_t  kMaxGamepadName = 63;

  struct GamepadSnapshot {
    uint32_t ms;
    uint32_t seq;
    uint32_t axisCount;
    uint32_t buttonCount;
    float    axes[20];
    bool     buttons[20];
    uint32_t valueCount;        // buttons that also carry an analog value
    uint8_t  buttonValues[20];  // 0-255, e.g. analog triggers
  };

  class IGamepadSource {
  public:
    virtual GamepadSnapshot read() const = 0;
    // Name the Driver Station reported for the gamepad on this port and
    // whether the browser uses the W3C standard mapping. False when none.
    virtual bool info(char* name, size_t cap, bool* standard) const {
      (void)standard;
      if (name && cap) name[0] = '\0';
      return false;
    }
    virtual ~IGamepadSource() {}
  };

  class GamepadService : public IGamepadSource {
  public:
    GamepadService(){
      _cur = 0;
      GamepadSnapshot z{};
      _buf[0] = z;
      _buf[1] = z;
      _timeout_ms = PROBOT_INPUT_TIMEOUT_MS;
    }

    // Written from two tasks (WS/HTTP handlers push frames, sysloop
    // zeroes state on owner release); read from user code on the other
    // core — everything goes through one short critical section.
    void write(uint32_t now_ms, const float* axes, uint32_t nAxis, const bool* buttons, uint32_t nButton,
               const uint8_t* values = nullptr, uint32_t nValue = 0){
      probot::core::MuxGuard guard(_mux);
      uint32_t cur = _cur;
      uint32_t w   = 1u - cur;
      GamepadSnapshot s = _buf[cur];
      s.ms = now_ms;
      s.seq++;
      s.axisCount = (nAxis>20?20:nAxis);
      s.buttonCount = (nButton>20?20:nButton);
      s.valueCount = values ? (nValue>s.buttonCount?s.buttonCount:nValue) : 0;
      for (uint32_t i=0;i<s.axisCount;i++){ s.axes[i] = axes[i]; }
      for (uint32_t i=0;i<s.buttonCount;i++){ s.buttons[i] = buttons[i]; }
      for (uint32_t i=0;i<s.valueCount;i++){ s.buttonValues[i] = values[i]; }
      _buf[w] = s;
      _cur = w;
    }

    // Name and mapping of the gamepad on this port (from the Driver
    // Station 'N' frame); truncated to kMaxGamepadName bytes.
    void setInfo(const char* name, size_t len, bool standard){
      if (len > kMaxGamepadName) len = kMaxGamepadName;
      probot::core::MuxGuard guard(_mux);
      memcpy(_name, name, len);
      _name[len] = '\0';
      _standard = standard;
    }

    // A Driver Station frame arrived for this port. Neutral writes (gating,
    // stall, owner release) do not count, so the name and frame age follow
    // the gamepad, not the failsafe.
    void noteFrame(uint32_t now_ms){
      probot::core::MuxGuard guard(_mux);
      _frameMs = now_ms;
      _hasFrame = true;
    }

    // At least one Driver Station frame arrived since boot.
    bool hasFrame() const {
      probot::core::MuxGuard guard(_mux);
      return _hasFrame;
    }

    // Reported only while frames keep arriving: a gamepad that went
    // silent past the input timeout has no name.
    bool info(char* name, size_t cap, bool* standard) const override {
      if (name && cap) name[0] = '\0';
      char tmp[kMaxGamepadName + 1];
      bool std = false, has;
      uint32_t ms;
      {
        probot::core::MuxGuard guard(_mux);
        memcpy(tmp, _name, sizeof tmp);
        std = _standard;
        ms = _frameMs;
        has = _hasFrame;
      }
      if (!tmp[0] || !has || stale(ms)) return false;
      if (name && cap) {
        size_t n = strlen(tmp);
        if (n >= cap) n = cap - 1;
        memcpy(name, tmp, n);
        name[n] = '\0';
      }
      if (standard) *standard = std;
      return true;
    }

    void setTimeoutMs(uint32_t timeout_ms){
      __atomic_store_n(&_timeout_ms, timeout_ms, __ATOMIC_SEQ_CST);
    }

    // Time of the last Driver Station frame (0 = none yet). Unlike
    // read().ms it is not rewritten by neutral writes, so it measures true
    // joystick frame age for diagnostics.
    uint32_t lastFrameMs() const {
      probot::core::MuxGuard guard(_mux);
      return _hasFrame ? _frameMs : 0;
    }
    uint32_t lastWriteMs() const { return lastFrameMs(); }   // former name

    GamepadSnapshot read() const override {
      GamepadSnapshot s;
      {
        probot::core::MuxGuard guard(_mux);
        s = _buf[_cur];
      }
      if (stale(s.ms)){
        GamepadSnapshot z{};
        z.ms = millis();
        z.seq = s.seq;
        return z;
      }
      return s;
    }

  private:
    bool stale(uint32_t ms) const {
      uint32_t timeout_ms = __atomic_load_n(&_timeout_ms, __ATOMIC_SEQ_CST);
      return timeout_ms > 0 && (uint32_t)(millis() - ms) > timeout_ms;
    }

    mutable GamepadSnapshot _buf[2];
    mutable volatile uint32_t _cur;
    mutable volatile uint32_t _timeout_ms;
    char _name[kMaxGamepadName + 1] = {0};
    bool _standard = false;
    uint32_t _frameMs = 0;
    bool _hasFrame = false;
    probot::core::Mux _mux;
  };

  namespace detail {
    inline GamepadService g_gamepads[kMaxGamepads];
    inline GamepadService g_raw_gamepads[kMaxGamepads];
    inline GamepadService g_no_gamepad;   // never written: always neutral
  }

  // Gated input for user code: neutral unless the robot is in a started,
  // unstalled RUN phase. Use this for anything that drives actuators.
  // Ports 0-5; any other port reads as an always-neutral gamepad.
  inline GamepadService& gamepad(int port = 0){
    return port >= 0 && port < kMaxGamepads ? detail::g_gamepads[port] : detail::g_no_gamepad;
  }

  // Ungated input: the last frame the Driver Station sent, in any phase
  // (still neutral once it is older than PROBOT_INPUT_TIMEOUT_MS or the DS
  // owner is released). For menus, auto selection and button bindings while
  // disabled; never drive actuators from it.
  inline GamepadService& rawGamepad(int port = 0){
    return port >= 0 && port < kMaxGamepads ? detail::g_raw_gamepads[port] : detail::g_no_gamepad;
  }

  namespace detail {
    // One Driver Station frame for `port`: always to the raw service, to
    // the gated one only while inputs are enabled (neutral otherwise).
    inline void applyFrame(uint8_t port, uint32_t now, bool enabled,
                           const float* axes, uint32_t nA, const bool* buttons, uint32_t nB,
                           const uint8_t* values = nullptr, uint32_t nV = 0){
      if (port >= kMaxGamepads) return;
      g_raw_gamepads[port].write(now, axes, nA, buttons, nB, values, nV);
      g_raw_gamepads[port].noteFrame(now);
      if (enabled) g_gamepads[port].write(now, axes, nA, buttons, nB, values, nV);
      else g_gamepads[port].write(now, nullptr, 0, nullptr, 0);
      g_gamepads[port].noteFrame(now);
    }

    inline void setInfo(uint8_t port, const char* name, size_t len, bool standard){
      if (port >= kMaxGamepads) return;
      g_raw_gamepads[port].setInfo(name, len, standard);
      g_gamepads[port].setInfo(name, len, standard);
    }

    // Every gated port neutral; with `raw` (owner released), the ungated
    // ones too and the gamepad names are forgotten.
    inline void neutralizeAll(uint32_t now, bool raw){
      for (uint8_t p = 0; p < kMaxGamepads; ++p){
        g_gamepads[p].write(now, nullptr, 0, nullptr, 0);
        if (raw){
          g_raw_gamepads[p].write(now, nullptr, 0, nullptr, 0);
          setInfo(p, "", 0, false);
        }
      }
    }

    inline void setTimeoutMs(uint32_t ms){
      for (uint8_t p = 0; p < kMaxGamepads; ++p){
        g_gamepads[p].setTimeoutMs(ms);
        g_raw_gamepads[p].setTimeoutMs(ms);
      }
      g_no_gamepad.setTimeoutMs(ms);
    }

    // Newest frame on any port (0 = none yet), for joystick age diagnostics.
    inline uint32_t newestFrameMs(){
      uint32_t best = 0;
      for (uint8_t p = 0; p < kMaxGamepads; ++p){
        uint32_t ms = g_raw_gamepads[p].lastFrameMs();
        if (ms && (!best || (int32_t)(ms - best) > 0)) best = ms;
      }
      return best;
    }
  } // namespace detail
}
