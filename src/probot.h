#pragma once
#include <Arduino.h>

// FTC-style user OpMode hooks. The recurring loop and stop hooks are required:
// omitting any one produces a linker error. INIT-loop and START are advanced,
// optional hooks; an undefined weak declaration is bound to the empty default
// by the runtime. A sketch definition overrides it.
//
// [PB-E201] "undefined reference to `teleopLoop()`" (veya autonomousLoop/
//   autonomousStop/teleopStop): zorunlu bir hook sketch'te tanımlanmamış.
//   Boş da olsa dördünü tanımlayın. Docs: probotstudio.com/docs/hatalar/#pb-e201
// [PB-E202] "multiple definition of `setup()`/`loop()`": setup()/loop()
//   kütüphaneye aittir, sketch'te TANIMLANMAZ — hook'ları kullanın.
//   Docs: probotstudio.com/docs/hatalar/#pb-e202
//
// A framework layer (see <probot/framework.hpp>) defines PROBOT_FRAMEWORK
// before this header; every hook is then optional.
#ifdef PROBOT_FRAMEWORK
#define PROBOT_REQUIRED_HOOK __attribute__((weak))
#else
#define PROBOT_REQUIRED_HOOK
#endif
void autonomousInit() __attribute__((weak));
void autonomousInitLoop() __attribute__((weak));
void autonomousStart() __attribute__((weak));
void autonomousLoop() PROBOT_REQUIRED_HOOK;
void autonomousStop() PROBOT_REQUIRED_HOOK;
void teleopInit() __attribute__((weak));
void teleopInitLoop() __attribute__((weak));
void teleopStart() __attribute__((weak));
void teleopLoop() PROBOT_REQUIRED_HOOK;
void teleopStop() PROBOT_REQUIRED_HOOK;
// UTILITY: a third, optional OpMode for setup and maintenance code (sensor
// calibration, mechanism homing, tests). The Driver Station offers it only
// when utilityLoop() is defined or a framework lists a utility option.
void utilityInit() __attribute__((weak));
void utilityInitLoop() __attribute__((weak));
void utilityStart() __attribute__((weak));
void utilityLoop() __attribute__((weak));
void utilityStop() __attribute__((weak));
#undef PROBOT_REQUIRED_HOOK

// [PB-E106] "redefinition of `void robotInit()`" / "robotEnd()": these 0.3.x
//   hooks were removed in 0.4.0 and would silently never run. They are deleted
//   so an old sketch fails to compile. Move the code: setup into
//   teleopInit()/autonomousInit(), safe stop into teleopStop()/autonomousStop().
//   Docs: probotstudio.com/docs/hatalar/#pb-e106
void robotInit() = delete; // [PB-E106] removed in 0.4.0: move this code into teleopInit()/autonomousInit()
void robotEnd() = delete;  // [PB-E106] removed in 0.4.0: move this code into teleopStop()/autonomousStop()

// Config-free API (also usable from other .cpp files)
#include <probot/api.hpp>

// Core runtime
#include <probot/core/core_config.hpp>
#include <probot/core/wdt.hpp>
#include <probot/core/runtime.hpp>

// Driver station communication
#include <probot/io/gamepad.hpp>
#include <probot/io/joystick_api.hpp>
#include <probot/devices/leds/builtin.hpp>
#include <probot/robot/state.hpp>
#include <probot/robot/system.hpp>
#include <probot/telemetry/telemetry.hpp>
