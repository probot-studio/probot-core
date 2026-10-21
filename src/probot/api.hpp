#pragma once

// Config-free Probot API for any translation unit (.cpp files, libraries
// built on top of Probot). Needs none of the PROBOT_WIFI_* or battery macros
// and instantiates nothing per TU beyond inline variables.
//
// <probot.h> includes this and adds the runtime (WiFi AP, Driver Station,
// setup()/loop()); include <probot.h> from the sketch only.

#include <probot/version.hpp>
#include <probot/robot/state.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/core/loop_timing.hpp>
#include <probot/io/gamepad.hpp>
#include <probot/io/joystick_api.hpp>
#include <probot/telemetry/telemetry.hpp>
#include <probot/ds.hpp>
#include <probot/framework.hpp>
#include <probot/dashboard/dashboard.hpp>
