#pragma once
#include <limits>
#include <probot/robot/state.hpp>

// Config-free battery readings, declared in every configuration: library
// code and extra .cpp files can read them without the battery macros of
// the sketch (probot/io/battery.hpp).
namespace probot::io::battery {

namespace detail {
  // Last current sample of an INA sensor; NaN without one, before the
  // first sample, or when the sensor did not answer.
  inline volatile float g_currentA = std::numeric_limits<float>::quiet_NaN();
  // isLowVoltage() threshold in volts; 0 = off.
  inline volatile float g_lowVolts = 0.0f;

  inline bool isLowVoltage(float volts, float threshold) {
    return threshold > 0.0f && volts > 0.0f && volts < threshold;
  }
}

// Battery current in amps (+ = drawn from the battery, with INA VIN+ on the
// battery side and VIN- on the load side). NaN when there is no current
// sensor (PROBOT_BATTERY_INA) or it has no valid sample.
inline float currentAmps() { return detail::g_currentA; }

// Sets the isLowVoltage() threshold (like WPILib setBrownoutVoltage, but
// only a flag: outputs are not disabled). 0 turns it off, the default;
// false for a negative, NaN or >= 100 V value.
inline bool setLowVoltage(float volts) {
  if (!(volts >= 0.0f && volts < 100.0f)) return false;
  detail::g_lowVolts = volts;
  return true;
}

// The battery voltage the Driver Station shows (measured or fed with
// setBatteryVoltage()) is below the setLowVoltage() threshold. False while
// the threshold is off or there is no reading. The reading is smoothed and
// moves in 0.1 V steps, so a short sag under load may not show.
inline bool isLowVoltage() {
  return detail::isLowVoltage(probot::robot::state().read().batteryVoltage, detail::g_lowVolts);
}

} // namespace probot::io::battery
