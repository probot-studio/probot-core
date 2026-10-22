#pragma once
#include <stdint.h>
#include <Arduino.h>
#include <probot/core/lock.hpp>

// How long after the last received joystick packet axes/buttons keep
// their values before read() returns neutral. FRC uses 125 ms, FTC
// ~300 ms, WPILib XRP 500 ms; tighten for faster failsafe.
#ifndef PROBOT_INPUT_TIMEOUT_MS
#define PROBOT_INPUT_TIMEOUT_MS 500
#endif

namespace probot::io {
  struct GamepadSnapshot {
    uint32_t ms;
    uint32_t seq;
    uint32_t axisCount;
    uint32_t buttonCount;
    float    axes[20];
    bool     buttons[20];
  };

  class IGamepadSource {
  public:
    virtual GamepadSnapshot read() const = 0;
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
    void write(uint32_t now_ms, const float* axes, uint32_t nAxis, const bool* buttons, uint32_t nButton){
      probot::core::MuxGuard guard(_mux);
      uint32_t cur = _cur;
      uint32_t w   = 1u - cur;
      GamepadSnapshot s = _buf[cur];
      s.ms = now_ms;
      s.seq++;
      s.axisCount = (nAxis>20?20:nAxis);
      s.buttonCount = (nButton>20?20:nButton);
      for (uint32_t i=0;i<s.axisCount;i++){ s.axes[i] = axes[i]; }
      for (uint32_t i=0;i<s.buttonCount;i++){ s.buttons[i] = buttons[i]; }
      _buf[w] = s;
      _cur = w;
    }

    void setTimeoutMs(uint32_t timeout_ms){
      __atomic_store_n(&_timeout_ms, timeout_ms, __ATOMIC_SEQ_CST);
    }

    // Raw time of the last received packet — unlike read().ms this is
    // not rewritten when the data goes stale, so it measures true
    // joystick frame age for diagnostics.
    uint32_t lastWriteMs() const {
      probot::core::MuxGuard guard(_mux);
      return _buf[_cur].ms;
    }

    GamepadSnapshot read() const override {
      GamepadSnapshot s;
      {
        probot::core::MuxGuard guard(_mux);
        s = _buf[_cur];
      }
      uint32_t timeout_ms = __atomic_load_n(&_timeout_ms, __ATOMIC_SEQ_CST);
      if (timeout_ms > 0){
        uint32_t now_ms = millis();
        if ((uint32_t)(now_ms - s.ms) > timeout_ms){
          GamepadSnapshot z{};
          z.ms = now_ms;
          z.seq = s.seq;
          return z;
        }
      }
      return s;
    }

  private:
    mutable GamepadSnapshot _buf[2];
    mutable volatile uint32_t _cur;
    mutable volatile uint32_t _timeout_ms;
    probot::core::Mux _mux;
  };

  namespace detail {
    inline GamepadService g_gamepad_singleton;
    inline GamepadService g_raw_gamepad_singleton;
  }

  // Gated input for user code: neutral unless the robot is in a started,
  // unstalled RUN phase. Use this for anything that drives actuators.
  inline GamepadService& gamepad(){
    return detail::g_gamepad_singleton;
  }

  // Ungated input: the last frame the Driver Station sent, in any phase
  // (still neutral once it is older than PROBOT_INPUT_TIMEOUT_MS or the DS
  // owner is released). For menus, auto selection and button bindings while
  // disabled; never drive actuators from it.
  inline GamepadService& rawGamepad(){
    return detail::g_raw_gamepad_singleton;
  }
}
