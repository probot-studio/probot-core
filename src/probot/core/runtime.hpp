#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <probot/core/core_config.hpp>
#include <probot/core/wdt.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/robot/autonomous.hpp>

// Driver-station inactivity → STOP (1, default, safe) or just disconnect (0).
#ifndef PROBOT_DS_TIMEOUT_MS
#define PROBOT_DS_TIMEOUT_MS 10000
#endif
#ifndef PROBOT_DS_TIMEOUT_FORCE_STOP
#define PROBOT_DS_TIMEOUT_FORCE_STOP 1
#endif

// A single teleop/autonomous loop iteration that has not returned within this
// many ms is treated as STALLED: inputs are zeroed and the robot is held safe
// (no kill, no reboot — see the design notes below). Must stay below the
// hardware watchdog timeout.
#ifndef PROBOT_LOOP_DEADLINE_MS
#define PROBOT_LOOP_DEADLINE_MS 2000
#endif

// Hardware task watchdog timeout (seconds). Only the sysloop supervisor is
// subscribed, so this reboots ONLY on a library/supervisor wedge — never on
// user code (a wedged user loop is held safe, not rebooted, to preserve any
// relative/homed mechanism state). Keep it comfortably above the loop
// deadline so a legitimately slow iteration never trips it.
#ifndef PROBOT_WDT_TIMEOUT_S
#define PROBOT_WDT_TIMEOUT_S 8
#endif

// Emergency stop: after killing the user task we run robotEnd() in a fresh
// task; if robotEnd does not return within this budget (e.g. it touches a bus
// the kill orphaned) the chip reboots instead of hanging.
#ifndef PROBOT_ESTOP_END_MS
#define PROBOT_ESTOP_END_MS 500
#endif

// Optional library-owned enable/E-stop GPIO. Wire it to your motor drivers'
// enable lines (or a contactor). Driven HIGH at boot, LOW on emergency stop —
// a hardware kill path independent of how user code drives outputs. -1 = off.
#ifndef PROBOT_ESTOP_ENABLE_PIN
#define PROBOT_ESTOP_ENABLE_PIN -1
#endif

// Optional robot signal light (FRC-RSL style) on a plain digital pin. The
// library drives it: BLINK while the robot can move (teleop/autonomous),
// SOLID ON otherwise (disabled/stopped/e-stopped). -1 = off.
#ifndef PROBOT_RSL_PIN
#define PROBOT_RSL_PIN -1
#endif

#include <probot/robot/system.hpp>
#include <probot/robot/state.hpp>
#include <probot/telemetry/telemetry.hpp>
#include <probot/devices/leds/builtin.hpp>
#include <probot/io/gamepad.hpp>

namespace probot {
  void runtime_setup();
  // Terminal emergency stop: kill the user task, run robotEnd under a
  // watchdog, latch dead until reboot. Safe to call from any task.
  void emergencyStop();
}

// User hooks (provided by sketch)
void robotInit();
void robotEnd();
void teleopInit();
void teleopLoop();
void autonomousInit();
void autonomousLoop();
// Optional configuration hook. Runs once during boot, before the DS starts,
// so the autonomous catalog is available while the robot is still stopped.
void autonomousConfigure() __attribute__((weak));

namespace probot {
  namespace detail {
    using core::Mode;

    // requested mode: written by sysloop, read by the user task.
    inline volatile uint32_t g_requested_mode = (uint32_t)Mode::STOP;

    inline TaskHandle_t g_user_task    = nullptr;
    inline TaskHandle_t g_sysloop_task = nullptr;
    inline TaskHandle_t g_estop_task   = nullptr;

    // emergency-stop robotEnd watchdog
    inline volatile uint32_t g_estop_end_start = 0;
    inline volatile uint32_t g_estop_end_done  = 0;

    inline core::PhaseMachine g_machine;

    inline void robotInitHook(){
      ::robotInit();
    }

    inline void autonomousInitHook(){
      probot::autonomous::registry().dispatchInit(&::autonomousInit);
    }

    inline void autonomousLoopHook(){
      probot::autonomous::registry().dispatchPeriodic(&::autonomousLoop);
    }

    inline core::Hooks userHooks(){
      return core::Hooks{ &robotInitHook, &::robotEnd, &::teleopInit,
                          &::teleopLoop, &autonomousInitHook, &autonomousLoopHook };
    }

    inline void zeroInputs(){
      probot::io::gamepad().write(millis(), nullptr, 0, nullptr, 0);
    }

    // ── The single persistent user task (core 1) ──
    // Created once, NEVER deleted in normal operation. All six hooks run
    // here, in sequence, only at loop boundaries — so a Stop or phase change
    // can never interrupt user code mid-transaction and orphan a lock.
    inline void userTask(void*){
      auto hooks = userHooks();
      auto& rs = probot::robot::state();
      __atomic_store_n(&probot::robot::g_loop_heartbeat_ms, millis(), __ATOMIC_SEQ_CST);
      for(;;){
        Mode req = (Mode)__atomic_load_n(&g_requested_mode, __ATOMIC_SEQ_CST);
        g_machine.step(hooks, req, rs, millis(), &zeroInputs,
                       &probot::robot::g_loop_heartbeat_ms);
        __atomic_store_n(&probot::robot::g_loop_heartbeat_ms, millis(), __ATOMIC_SEQ_CST);
        vTaskDelay(pdMS_TO_TICKS(USER_LOOP_PERIOD_MS));
      }
    }

    // ── Emergency stop ──
    inline void estopEndTask(void*){
      ::robotEnd();
      __atomic_store_n(&g_estop_end_done, 1u, __ATOMIC_SEQ_CST);
      vTaskSuspend(NULL);
    }

    inline void setEnablePin(bool enabled){
#if PROBOT_ESTOP_ENABLE_PIN >= 0
      digitalWrite(PROBOT_ESTOP_ENABLE_PIN, enabled ? HIGH : LOW);
#else
      (void)enabled;
#endif
    }

    // The actual terminal emergency-stop sequence. Runs ONLY from the sysloop
    // (core 0) so it never deletes its own caller. Idempotent.
    inline void runEmergencyStop(){
      if (__atomic_exchange_n(&probot::robot::g_estop_latched, 1u, __ATOMIC_SEQ_CST)) return;
      setEnablePin(false);                 // hardware kill path, if wired
      // Kill the single user task. Safe for probot's own portMUX locks (they
      // can't be killed mid critical-section); only a user-held Wire/malloc
      // lock can orphan, acceptable because this path is terminal + reboot.
      if (g_user_task){
        vTaskDelete(g_user_task);
        g_user_task = nullptr;
      }
      // Run robotEnd() in a fresh task under the sysloop watchdog.
      __atomic_store_n(&g_estop_end_done, 0u, __ATOMIC_SEQ_CST);
      uint32_t t = millis(); if (t == 0) t = 1;
      __atomic_store_n(&g_estop_end_start, t, __ATOMIC_SEQ_CST);
      xTaskCreatePinnedToCore(estopEndTask, "estop", STACK_USER, NULL,
                              PRIO_USER, &g_estop_task, CORE_CTRL);
      probot::robot::state().setStatus(millis(), probot::robot::Status::STOP);
      probot::telemetry::println("!! EMERGENCY STOP — robot disabled, reboot required");
      Serial.println("[SYS  ] EMERGENCY STOP");
    }

    inline void updateLed(bool estop){
      static bool on = false;
      on = !on;
      auto s = probot::robot::state().read();
      uint8_t r = 0, g = 0, b = 0;

      if (estop){
        r = 255;                         // solid red — terminal
      } else if (s.deadlineMiss){
        if (on) r = 255;                 // blinking red — stalled (held safe)
      } else {
        switch (s.phase){
          case probot::robot::Phase::NOT_INIT:
            b = 255;
            if (s.clientCount > 0 && !on) b = 0;   // blink blue when a client is connected
            break;
          case probot::robot::Phase::INITED:
            r = 255; g = 255;            // solid yellow
            break;
          case probot::robot::Phase::AUTONOMOUS:
            if (on){ r = 255; g = 128; } // blink orange
            break;
          case probot::robot::Phase::TELEOP:
            if (on) g = 255;             // blink green
            break;
        }
      }
      builtinled::render(r, g, b);       // single caller (sysloop): paints status

#if PROBOT_RSL_PIN >= 0
      // Robot signal light: blink while the robot can move, else solid on.
      bool moving = !estop && (s.phase == probot::robot::Phase::TELEOP ||
                               s.phase == probot::robot::Phase::AUTONOMOUS);
      digitalWrite(PROBOT_RSL_PIN, moving ? (on ? HIGH : LOW) : HIGH);
#endif
    }

    inline void sysloopTask(void*){
      using probot::robot::Status;
      using probot::robot::Phase;
#ifdef ESP32
      esp_task_wdt_add(NULL);
#endif
      core::Supervisor sup;
      uint32_t lastLed = 0;
      for(;;){
#ifdef ESP32
        esp_task_wdt_reset();
#endif
        uint32_t now = millis();
        bool estop = __atomic_load_n(&probot::robot::g_estop_latched, __ATOMIC_SEQ_CST) != 0;

        // Deliberate reboot (UI "reboot to clear estop" button).
        if (__atomic_load_n(&probot::robot::g_reboot_requested, __ATOMIC_SEQ_CST)){
          Serial.println("[SYS  ] reboot requested");
          delay(50);
          ESP.restart();
        }

        // Run the terminal emergency-stop sequence once, from here (so it is
        // serialized and off the HTTP task).
        if (!estop && __atomic_load_n(&probot::robot::g_estop_requested, __ATOMIC_SEQ_CST)){
          runEmergencyStop();
          estop = true;
        }

        // estop robotEnd watchdog: if it didn't return in time, reboot
        // (the kill may have orphaned a bus robotEnd needs).
        if (estop){
          uint32_t es = __atomic_load_n(&g_estop_end_start, __ATOMIC_SEQ_CST);
          if (es != 0 && !__atomic_load_n(&g_estop_end_done, __ATOMIC_SEQ_CST) &&
              (int32_t)(now - es) >= (int32_t)PROBOT_ESTOP_END_MS){
            Serial.println("[SYS  ] estop robotEnd timed out -> restart");
            delay(20);
            ESP.restart();
          }
        }

        // Translate buttons → requested mode (and enforce the auto period).
        Mode req = sup.update(probot::robot::state(), now, estop);
        __atomic_store_n(&g_requested_mode, (uint32_t)req, __ATOMIC_SEQ_CST);

        // Stall watchdog (halt-safe). A loop iteration past the deadline →
        // zero inputs, flag stalled (red LED), hold. No kill, no reboot.
        if (!estop){
          auto s = probot::robot::state().read();
          uint32_t hb = __atomic_load_n(&probot::robot::g_loop_heartbeat_ms, __ATOMIC_SEQ_CST);
          bool stalled = core::isStalled(s.phase, now, hb, PROBOT_LOOP_DEADLINE_MS);
          if (stalled && !s.deadlineMiss){
            probot::robot::state().setDeadlineMiss(now, true);
            zeroInputs();
            probot::telemetry::println("!! LOOP STALLED — inputs zeroed, holding safe (no reboot)");
          } else if (!stalled && s.deadlineMiss){
            probot::robot::state().setDeadlineMiss(now, false);   // recovered
          }
        }

        // DS connection heartbeat: no activity → stop and/or disconnect.
        if (!estop){
          uint32_t dsAct = __atomic_load_n(&probot::robot::g_ds_last_activity_ms, __ATOMIC_SEQ_CST);
          if (dsAct != 0 && probot::robot::state().status() != Status::STOP &&
              (int32_t)(now - dsAct) > (int32_t)PROBOT_DS_TIMEOUT_MS){
            Serial.printf("[SYS  ] DS timeout: no activity for %lu ms\n", (unsigned long)(now - dsAct));
#if PROBOT_DS_TIMEOUT_FORCE_STOP
            probot::telemetry::println("!! DS CONNECTION LOST — stopping robot");
            probot::robot::state().setStatus(now, Status::STOP);
#else
            probot::telemetry::println("!! DS CONNECTION LOST — joystick neutral, waiting reconnect");
#endif
#ifdef ESP32
            if (probot::driverstation::detail::g_driver_station){
              probot::driverstation::detail::g_driver_station->forceDisconnect(now);
            }
#endif
            __atomic_store_n(&probot::robot::g_ds_last_activity_ms, 0u, __ATOMIC_SEQ_CST);
          }
        }

#ifdef ESP32
        if (probot::driverstation::detail::g_driver_station){
          probot::driverstation::detail::g_driver_station->expireOwnerIfIdle();
          probot::driverstation::detail::g_driver_station->processDns();
        }
#endif

        if (now - lastLed >= 500){
          lastLed = now;
          updateLed(estop);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }
  } // namespace detail

  // Request a terminal emergency stop. Safe to call from ANY task, including
  // a user hook: it only sets a flag. The sysloop runs the real sequence
  // (kill the user task, run robotEnd under a watchdog, latch until reboot),
  // so it never deletes its own caller.
  inline void emergencyStop(){
    __atomic_store_n(&probot::robot::g_estop_requested, 1u, __ATOMIC_SEQ_CST);
  }

  inline void runtime_setup(){
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[Probot] Core0=DS+SYS, Core1=USER (cooperative lifecycle)");

#if PROBOT_ESTOP_ENABLE_PIN >= 0
    pinMode(PROBOT_ESTOP_ENABLE_PIN, OUTPUT);
    digitalWrite(PROBOT_ESTOP_ENABLE_PIN, HIGH);   // enabled
#endif
#if PROBOT_RSL_PIN >= 0
    pinMode(PROBOT_RSL_PIN, OUTPUT);
    digitalWrite(PROBOT_RSL_PIN, HIGH);            // solid on until moving
#endif

    wdt_init_no_idle(PROBOT_WDT_TIMEOUT_S, true);

    // Register autonomous routines before the Driver Station comes online.
    // Older sketches without this optional hook continue to use the legacy
    // autonomousInit/autonomousLoop callbacks.
    if (autonomousConfigure) {
      probot::autonomous::clearRoutines();
      autonomousConfigure();
      probot::autonomous::registry().finishRegistration();
      if (probot::autonomous::registry().registrationError())
        Serial.println("[AUTO ] Routine catalog has invalid or duplicate entries");
    }

#ifdef ESP32
    probot::driverstation::start_driver_station();
#endif

    // Persistent user task (core 1) and sysloop supervisor (core 0).
    xTaskCreatePinnedToCore(detail::userTask, "user", STACK_USER, NULL,
                            PRIO_USER, &detail::g_user_task, CORE_CTRL);
    xTaskCreatePinnedToCore(detail::sysloopTask, "sysloop", STACK_CTRL, NULL,
                            PRIO_CTRL, &detail::g_sysloop_task, CORE_UI);
  }

} // namespace probot

#if defined(ARDUINO)
__attribute__((used)) inline void setup(){
  probot::runtime_setup();
}

__attribute__((used)) inline void loop(){
  delay(100);
}
#endif
