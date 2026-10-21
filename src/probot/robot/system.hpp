#pragma once
#include <probot/robot/state.hpp>
#include <probot/io/gamepad.hpp>

#ifdef ESP32
#include <driverstation/esp32s3/driver_station_esp32.hpp>
#endif

namespace probot::driverstation {
  namespace detail {
#ifdef ESP32
    inline probot::driverstation::esp32::DriverStation* g_driver_station = nullptr;
#endif
  } // namespace detail

  inline void start_driver_station(){
  #ifdef ESP32
    static probot::driverstation::esp32::DriverStation ds(probot::robot::state(), probot::io::gamepad());
    detail::g_driver_station = &ds;
    ds.begin();
  #endif
  }
}
