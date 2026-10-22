#pragma once
#include <cmath>
#include <probot/io/gamepad.hpp>
#include <probot/io/joystick_mappings/mapping.hpp>

namespace probot::io::joystick_api {

struct Options {
  float deadzone = 0.08f;
  bool  invertY  = true;
  bool  useMappingInvert = true;
};

class Joystick {
public:
  explicit Joystick(const IGamepadSource* source, Options opts = {})
  : _source(source), _opts(opts) {}

  // Low-level
  inline uint32_t getSeq() const { return snapshot().seq; }
  inline uint32_t getMs() const  { return snapshot().ms; }
  inline uint32_t getAxisCount() const { return snapshot().axisCount; }
  inline uint32_t getButtonCount() const { return snapshot().buttonCount; }
  inline bool isConnected() const { auto s=snapshot(); return (s.axisCount + s.buttonCount) > 0; }

  inline float getRawAxis(int i) const {
    auto s = snapshot();
    if (i < 0 || (uint32_t)i >= s.axisCount) return 0.0f;
    return s.axes[i];
  }

  inline bool getRawButton(int i) const {
    auto s = snapshot();
    if (i < 0 || (uint32_t)i >= s.buttonCount) return false;
    return s.buttons[i];
  }

  // Analog value of button i in 0..1 (triggers); 1/0 from the pressed
  // state when the Driver Station sent no analog value for it.
  inline float getRawButtonValue(int i) const {
    auto s = snapshot();
    if (i < 0 || (uint32_t)i >= s.buttonCount) return 0.0f;
    if ((uint32_t)i < s.valueCount) return s.buttonValues[i] / 255.0f;
    return s.buttons[i] ? 1.0f : 0.0f;
  }

  // Browser name of the gamepad (e.g. "Xbox 360 Controller (XInput STANDARD
  // GAMEPAD)") into `out`; false and "" when none is connected.
  inline bool getName(char* out, size_t cap) const { return _source->info(out, cap, nullptr); }

  // The browser reports the W3C standard mapping (the button and axis
  // order the "standard" profile assumes).
  inline bool isStandardMapping() const {
    bool standard = false;
    return _source->info(nullptr, 0, &standard) && standard;
  }

  // WPILib-style POV: -1 if none, else 0/90/180/270. Gamepads with D-pad
  // buttons also report the diagonals 45/135/225/315.
  inline int getPOV() const {
    const auto& m = joystick_mapping::getActive();
    auto s = snapshot();
    if (m.dpadFromAxis && m.dpadAxis >= 0 && (uint32_t)m.dpadAxis < s.axisCount) {
      float v = s.axes[m.dpadAxis];
      if (v >= -1.0f && v < -0.5f) return 0;    // Up
      if (v >  0.0f && v <  0.5f)  return 90;   // Right
      if (v >  0.5f && v <= 1.0f)  return 180;  // Down
      if (v > -0.5f && v <  0.0f)  return 270;  // Left
      return -1;
    }
    auto inRange = [&](int idx){ return idx >= 0 && (uint32_t)idx < s.buttonCount; };
    bool mappingAvailable = inRange(m.DPadUp) || inRange(m.DPadRight) || inRange(m.DPadDown) || inRange(m.DPadLeft);
    if (mappingAvailable) {
      auto on = [&](int idx){ return inRange(idx) && s.buttons[idx]; };
      // Opposite directions cancel out, as on a hat switch.
      int dx = (on(m.DPadRight) ? 1 : 0) - (on(m.DPadLeft) ? 1 : 0);
      int dy = (on(m.DPadUp) ? 1 : 0) - (on(m.DPadDown) ? 1 : 0);
      static const int kPov[3][3] = {   // [dy + 1][dx + 1]
        {225, 180, 135},
        {270,  -1,  90},
        {315,   0,  45},
      };
      return kPov[dy + 1][dx + 1];
    }
    if (s.buttonCount > 7) {
      // Legacy fallback 4..7
      if (s.buttons[4]) return 0;
      if (s.buttons[7]) return 90;
      if (s.buttons[5]) return 180;
      if (s.buttons[6]) return 270;
    }
    return -1;
  }

  // High-level axes using mapping
  inline float getLeftX()  const { return dz(getRawAxis(joystick_mapping::getActive().leftX)); }
  inline float getLeftY()  const {
    const auto& m = joystick_mapping::getActive();
    float y = getRawAxis(m.leftY);
    bool invert = _opts.invertY || (_opts.useMappingInvert && m.invertLeftY);
    return dz(invert ? -y : y);
  }
  inline float getRightX() const { return dz(getRawAxis(joystick_mapping::getActive().rightX)); }
  inline float getRightY() const {
    const auto& m = joystick_mapping::getActive();
    float y = getRawAxis(m.rightY);
    bool invert = _opts.invertY || (_opts.useMappingInvert && m.invertRightY);
    return dz(invert ? -y : y);
  }

  // Analog triggers can rest slightly above 0; the stick deadzone applies.
  inline float getLeftTriggerAxis()  const { return dz(getRawButtonValue(joystick_mapping::getActive().LT)); }
  inline float getRightTriggerAxis() const { return dz(getRawButtonValue(joystick_mapping::getActive().RT)); }

  // High-level buttons (Xbox names)
  inline bool getA() const { return getRawButton(joystick_mapping::getActive().A); }
  inline bool getB() const { return getRawButton(joystick_mapping::getActive().B); }
  inline bool getX() const { return getRawButton(joystick_mapping::getActive().X); }
  inline bool getY() const { return getRawButton(joystick_mapping::getActive().Y); }
  inline bool getLB() const { return getRawButton(joystick_mapping::getActive().LB); }
  inline bool getRB() const { return getRawButton(joystick_mapping::getActive().RB); }
  inline bool getBack() const { return getRawButton(joystick_mapping::getActive().Back); }
  inline bool getStart() const { return getRawButton(joystick_mapping::getActive().Start); }
  inline bool getOptions() const { return getRawButton(joystick_mapping::getActive().Options); }
  inline bool getLeftStickButton() const { return getRawButton(joystick_mapping::getActive().LStick); }
  inline bool getRightStickButton() const { return getRawButton(joystick_mapping::getActive().RStick); }

  // High-level buttons (PlayStation synonyms)
  inline bool getCross() const { return getA(); }
  inline bool getCircle() const { return getB(); }
  inline bool getSquare() const { return getX(); }
  inline bool getTriangle() const { return getY(); }

  // High-level D-Pad
  // A direction also holds on its two diagonals (up: 315, 0 and 45).
  inline bool getDpadUp() const    { return povNear(getPOV(), 0); }
  inline bool getDpadRight() const { return povNear(getPOV(), 90); }
  inline bool getDpadDown() const  { return povNear(getPOV(), 180); }
  inline bool getDpadLeft() const  { return povNear(getPOV(), 270); }

private:
  static bool povNear(int pov, int dir) {
    if (pov < 0) return false;
    int d = (pov - dir + 360) % 360;
    return d == 0 || d == 45 || d == 315;
  }
  inline float dz(float v) const { return (std::fabs(v) < _opts.deadzone) ? 0.0f : v; }
  inline GamepadSnapshot snapshot() const { return _source->read(); }

  const IGamepadSource* _source;
  Options _opts;
};

inline Joystick makeDefault(const Options& opts = {}) {
  return Joystick(&probot::io::gamepad(), opts);
}

// The gamepad on Driver Station port 0-5 (0 is the one makeDefault() reads).
// Any other port reads as an always-neutral gamepad.
inline Joystick makeForPort(int port, const Options& opts = {}) {
  return Joystick(&probot::io::gamepad(port), opts);
}

} // namespace probot::io::joystick_api
