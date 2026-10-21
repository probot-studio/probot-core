#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace probot {
  // Core 0 runs WiFi/httpd/sysloop ("UI"), core 1 runs user code ("CTRL").
  constexpr int CORE_UI   = 0;
  constexpr int CORE_CTRL = 1;

  constexpr UBaseType_t PRIO_CTRL  = 4;
  constexpr UBaseType_t PRIO_USER  = 1;

  constexpr uint32_t STACK_CTRL  = 4096;
  // One persistent user task hosts every FTC-style OpMode hook.
  // Override with PROBOT_USER_STACK (bytes) before including probot.h.
#ifndef PROBOT_USER_STACK
#define PROBOT_USER_STACK 8192
#endif
  constexpr uint32_t STACK_USER  = PROBOT_USER_STACK;
  static_assert(STACK_USER >= 4096,
                "[PB-E107] PROBOT_USER_STACK must be at least 4096 bytes. Docs: probotstudio.com/docs/hatalar/#pb-e107");
}

// User cadence: how often initLoop/loop hooks are called (~50 Hz).
#ifndef USER_LOOP_PERIOD_MS
#define USER_LOOP_PERIOD_MS 20
#endif
