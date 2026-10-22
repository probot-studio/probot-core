#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <probot/core/core_config.hpp>
#include <probot/core/wdt.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/core/loop_timing.hpp>
#include <probot/core/status_led.hpp>
#include <probot/io/battery.hpp>
#include <probot/framework.hpp>
#include <probot/dashboard/dashboard.hpp>

// Driver-station inactivity → STOP (1, default, safe) or just disconnect (0).
#ifndef PROBOT_DS_TIMEOUT_MS
#define PROBOT_DS_TIMEOUT_MS 10000
#endif
#ifndef PROBOT_DS_TIMEOUT_FORCE_STOP
#define PROBOT_DS_TIMEOUT_FORCE_STOP 1
#endif

// Dashboard topic store limits (probot/dashboard/dashboard.hpp). Memory is
// taken from the heap only once something is published.
#ifndef PROBOT_DASH_MAX_TOPICS
#define PROBOT_DASH_MAX_TOPICS 128
#endif
#ifndef PROBOT_DASH_MAX_SCHEMAS
#define PROBOT_DASH_MAX_SCHEMAS 32
#endif
#ifndef PROBOT_DASH_MAX_VALUE
#define PROBOT_DASH_MAX_VALUE 1024
#endif
#ifndef PROBOT_DASH_POOL_BYTES
#define PROBOT_DASH_POOL_BYTES 32768
#endif
#ifndef PROBOT_DASH_WRITE_QUEUE
#define PROBOT_DASH_WRITE_QUEUE 2048
#endif
static_assert(PROBOT_DASH_MAX_SCHEMAS >= 0 && PROBOT_DASH_MAX_SCHEMAS <= 255,
              "[PB-E108] PROBOT_DASH_MAX_SCHEMAS must be 0-255. Docs: probotstudio.com/docs/hatalar/#pb-e108");
static_assert(PROBOT_DASH_MAX_TOPICS >= 1 && PROBOT_DASH_MAX_TOPICS <= 255,
              "[PB-E108] PROBOT_DASH_MAX_TOPICS must be 1-255. Docs: probotstudio.com/docs/hatalar/#pb-e108");
static_assert(PROBOT_DASH_MAX_VALUE >= 16 && PROBOT_DASH_MAX_VALUE <= 16384,
              "[PB-E108] PROBOT_DASH_MAX_VALUE must be 16-16384 bytes. Docs: probotstudio.com/docs/hatalar/#pb-e108");

// Robot console ring (probot/telemetry/console.hpp), taken from the heap.
#ifndef PROBOT_CONSOLE_BYTES
#define PROBOT_CONSOLE_BYTES 4096
#endif
static_assert(PROBOT_CONSOLE_BYTES >= 2048 && PROBOT_CONSOLE_BYTES <= 65536,
              "[PB-E110] PROBOT_CONSOLE_BYTES must be 2048-65536 bytes. Docs: probotstudio.com/docs/hatalar/#pb-e110");

// PROBOT_LOOP_DEADLINE_MS / PROBOT_HOOK_DEADLINE_MS: see lifecycle.hpp.

// Hardware task watchdog timeout (seconds). Only the sysloop supervisor is
// subscribed, so this reboots ONLY on a library/supervisor wedge — never on
// user code (a wedged user loop is held safe, not rebooted, to preserve any
// relative/homed mechanism state). Keep it comfortably above the loop
// deadline so a legitimately slow iteration never trips it.
#ifndef PROBOT_WDT_TIMEOUT_S
#define PROBOT_WDT_TIMEOUT_S 8
#endif

// Emergency stop: after killing the user task we run the active OpMode's
// stop() in a fresh task. If it does not return within this budget (e.g. it
// touches a bus the kill orphaned), the chip reboots instead of hanging.
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
// library drives it: BLINK only in a RUN phase,
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
}

namespace probot {
  namespace detail {
    using core::DesiredState;
    using core::OpMode;
    using core::Stage;

    inline uint32_t packRequested(DesiredState s) {
      return (static_cast<uint32_t>(s.stage) << 8) | static_cast<uint32_t>(s.mode);
    }
    inline DesiredState unpackRequested(uint32_t raw) {
      return { static_cast<OpMode>(raw & 0xffu), static_cast<Stage>((raw >> 8) & 0xffu) };
    }

    // Desired state: written by sysloop, observed by the persistent user task.
    inline volatile uint32_t g_requested_state =
      packRequested({ OpMode::TELEOP, Stage::STOPPED });

    inline TaskHandle_t g_user_task    = nullptr;
    inline TaskHandle_t g_sysloop_task = nullptr;
    inline TaskHandle_t g_estop_task   = nullptr;

    // Emergency-stop active-stop watchdog and captured OpMode.
    inline volatile uint32_t g_estop_stop_start    = 0;
    inline volatile uint32_t g_estop_stop_done     = 0;
    inline volatile uint32_t g_estop_stop_required = 0;
    inline volatile uint32_t g_estop_call_stop     = 0;
    inline volatile uint32_t g_estop_stop_mode     = (uint32_t)OpMode::TELEOP;

    inline core::PhaseMachine g_machine;

    // Framework hooks, frozen by runtime_setup() before any task starts.
    inline framework::Hooks g_framework{ nullptr, nullptr, nullptr, nullptr };

    inline void emptyHook() {}

    // Every hook may be a weak, undefined symbol (always for the optional
    // ones; also for the required ones when PROBOT_FRAMEWORK is defined).
    // Taking the pointer as a parameter keeps -Waddress quiet for the
    // strong declarations.
    inline void (*orEmpty(void (*fn)()))() { return fn ? fn : &emptyHook; }

    inline core::Hooks userHooks(){
      return core::Hooks{
        orEmpty(&::autonomousInit),
        orEmpty(&::autonomousInitLoop),
        orEmpty(&::autonomousStart),
        orEmpty(&::autonomousLoop),
        orEmpty(&::autonomousStop),
        orEmpty(&::teleopInit),
        orEmpty(&::teleopInitLoop),
        orEmpty(&::teleopStart),
        orEmpty(&::teleopLoop),
        orEmpty(&::teleopStop),
        orEmpty(&::utilityInit),
        orEmpty(&::utilityInitLoop),
        orEmpty(&::utilityStart),
        orEmpty(&::utilityLoop),
        orEmpty(&::utilityStop)
      };
    }

    inline void primeLoopHeartbeat(){
      uint32_t t = millis();
      __atomic_store_n(&probot::robot::g_loop_heartbeat_ms, t == 0 ? 1u : t, __ATOMIC_SEQ_CST);
    }

    inline void zeroInputs(){
      probot::io::detail::neutralizeAll(millis(), false);
    }

    inline uint32_t userClockNow(){ return millis(); }

    // ── The single persistent user task (core 1) ──
    // Created once, NEVER deleted in normal operation. All hooks run
    // here, in sequence, only at loop boundaries — so a Stop or phase change
    // can never interrupt user code mid-transaction and orphan a lock.
    // Framework preCycle/postCycle wrap every iteration (see
    // PhaseMachine::cycle).
    inline void userTask(void*){
      auto hooks = userHooks();
      const framework::Hooks fw = g_framework;
      auto& rs = probot::robot::state();
      primeLoopHeartbeat();
      if (fw.boot) fw.boot();
      core::LoopPacer pacer;
      pacer.start(xTaskGetTickCount());
      for(;;){
        DesiredState req = unpackRequested(
          __atomic_load_n(&g_requested_state, __ATOMIC_SEQ_CST));
        const DesiredState before = g_machine.current();
        g_machine.cycle(hooks, fw.preCycle, fw.postCycle, req, rs, millis(), &zeroInputs,
                        &probot::robot::g_loop_heartbeat_ms, &userClockNow);

        TickType_t period = pdMS_TO_TICKS(probot::loopPeriodMs());
        bool overrun = false;
        TickType_t sleep = pacer.afterCycle(xTaskGetTickCount(), period ? period : 1,
                                            g_machine.current() != before, &overrun);
        if (overrun) __atomic_add_fetch(&core::g_loop_overruns, 1u, __ATOMIC_SEQ_CST);
        // The sleep is not user code: stamp the heartbeat past it so a long
        // period never reads as a stall.
        __atomic_store_n(&probot::robot::g_loop_heartbeat_ms,
                         core::wakeStampMs(millis(), sleep, portTICK_PERIOD_MS), __ATOMIC_SEQ_CST);
        vTaskDelay(sleep);
      }
    }

    // ── Emergency stop ──
    // Runs on core 1, so it starts only once the deleted user task is off the
    // CPU. The framework output kill comes first; both share the
    // PROBOT_ESTOP_END_MS budget (PB-E305).
    inline void estopStopTask(void*){
      if (g_framework.emergencyStop) g_framework.emergencyStop();
      if (__atomic_load_n(&g_estop_call_stop, __ATOMIC_SEQ_CST)) {
        OpMode mode = (OpMode)__atomic_load_n(&g_estop_stop_mode, __ATOMIC_SEQ_CST);
        orEmpty(core::pick(mode, &::autonomousStop, &::teleopStop, &::utilityStop))();
      }
      __atomic_store_n(&g_estop_stop_done, 1u, __ATOMIC_SEQ_CST);
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
      auto active = probot::robot::state().read();
      bool stopRequired = core::isActivePhase(active.phase);
      OpMode activeMode = core::modeOfPhase(active.phase);
      setEnablePin(false);                 // hardware kill path, if wired
      // Kill the single user task. Safe for probot's own portMUX locks (they
      // can't be killed mid critical-section); only a user-held Wire/malloc
      // lock can orphan, acceptable because this path is terminal + reboot.
      if (g_user_task){
        vTaskDelete(g_user_task);
        g_user_task = nullptr;
      }
      // STOPPED/TRANSITION has no active OpMode, hence no hook. Otherwise run
      // exactly that mode's stop hook in a fresh task under the watchdog,
      // after the framework emergencyStop callback if one is installed.
      bool taskRequired = stopRequired || g_framework.emergencyStop != nullptr;
      __atomic_store_n(&g_estop_call_stop, stopRequired ? 1u : 0u, __ATOMIC_SEQ_CST);
      __atomic_store_n(&g_estop_stop_required, taskRequired ? 1u : 0u, __ATOMIC_SEQ_CST);
      __atomic_store_n(&g_estop_stop_mode, (uint32_t)activeMode, __ATOMIC_SEQ_CST);
      __atomic_store_n(&g_estop_stop_done, taskRequired ? 0u : 1u, __ATOMIC_SEQ_CST);
      if (taskRequired) {
        uint32_t t = millis(); if (t == 0) t = 1;
        __atomic_store_n(&g_estop_stop_start, t, __ATOMIC_SEQ_CST);
        xTaskCreatePinnedToCore(estopStopTask, "estop", STACK_USER, NULL,
                                PRIO_USER, &g_estop_task, CORE_CTRL);
      }
      probot::robot::state().setStatus(millis(), probot::robot::Status::STOP);
      probot::telemetry::println("!! [PB-E302] EMERGENCY STOP — robot disabled, reboot required — docs/hatalar#pb-e302");
      Serial.println("[SYS  ] [PB-E302] EMERGENCY STOP — docs: probotstudio.com/docs/hatalar/#pb-e302");
    }

    inline void updateLed(bool estop){
      static bool on = false;
      on = !on;
      auto s = probot::robot::state().read();
      core::Rgb c = core::statusColor(s, estop, on);
      builtinled::render(c.r, c.g, c.b); // single caller (sysloop): paints status

#if PROBOT_RSL_PIN >= 0
      // Robot signal light: blink while the robot can move, else solid on.
      digitalWrite(PROBOT_RSL_PIN, core::rslBlinking(s.phase, estop) ? (on ? HIGH : LOW) : HIGH);
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

        // Active stop watchdog: the task kill may have orphaned a bus lock
        // needed by the user's stop hook.
        if (estop){
          uint32_t es = __atomic_load_n(&g_estop_stop_start, __ATOMIC_SEQ_CST);
          if (__atomic_load_n(&g_estop_stop_required, __ATOMIC_SEQ_CST) && es != 0 &&
              !__atomic_load_n(&g_estop_stop_done, __ATOMIC_SEQ_CST) &&
              (int32_t)(now - es) >= (int32_t)PROBOT_ESTOP_END_MS){
            Serial.println("[SYS  ] [PB-E305] estop stop hook timed out -> restart — docs: probotstudio.com/docs/hatalar/#pb-e305");
            delay(20);
            ESP.restart();
          }
        }

        // Translate command state → desired OpMode/stage and enforce auto time.
        DesiredState req = sup.update(probot::robot::state(), now, estop);
        __atomic_store_n(&g_requested_state, packRequested(req), __ATOMIC_SEQ_CST);

        // Stall watchdog (halt-safe). A loop iteration past the deadline →
        // zero inputs, flag stalled (red LED), hold. No kill, no reboot.
        if (!estop){
          auto s = probot::robot::state().read();
          uint32_t hb = __atomic_load_n(&probot::robot::g_loop_heartbeat_ms, __ATOMIC_SEQ_CST);
          bool stalled = core::isStalled(s.phase, now, hb, PROBOT_LOOP_DEADLINE_MS);
          if (stalled && !s.deadlineMiss){
            probot::robot::state().setDeadlineMiss(now, true);
            zeroInputs();
            probot::telemetry::println("!! [PB-E301] LOOP STALLED — inputs zeroed, holding safe (no reboot) — docs/hatalar#pb-e301");
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
            probot::telemetry::println("!! [PB-E303] DS CONNECTION LOST — stopping robot — docs/hatalar#pb-e303");
            probot::robot::state().setStatus(now, Status::STOP);
#else
            probot::telemetry::println("!! [PB-E304] DS CONNECTION LOST — joystick neutral, waiting reconnect — docs/hatalar#pb-e304");
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
    detail::g_framework = framework::seal();
    // gamepad.hpp is config-free and may be included by several TUs, so the
    // sketch's PROBOT_INPUT_TIMEOUT_MS is applied here, not by whichever
    // TU happens to construct the services.
    probot::io::detail::setTimeoutMs(PROBOT_INPUT_TIMEOUT_MS);
    __atomic_store_n(&core::g_loop_period_default_ms, (uint32_t)USER_LOOP_PERIOD_MS, __ATOMIC_SEQ_CST);
    __atomic_store_n(&probot::robot::g_utility_hooks,
                     detail::orEmpty(&::utilityLoop) != &detail::emptyHook ? 1u : 0u, __ATOMIC_SEQ_CST);
    probot::dashboard::detail::configure({ PROBOT_DASH_MAX_TOPICS, PROBOT_DASH_MAX_SCHEMAS,
                                           PROBOT_DASH_MAX_VALUE, PROBOT_DASH_POOL_BYTES,
                                           PROBOT_DASH_WRITE_QUEUE });
    // Before the Driver Station starts: reserve() moves the records, which
    // restarts any reader from the oldest line.
    if (!probot::console::detail::reserve(PROBOT_CONSOLE_BYTES)) {
      if (!probot::console::detail::g_ring.data && !probot::console::detail::reserve(probot::console::detail::kMinBytes))
        probot::console::detail::g_noMemory = true;   // no allocation attempts per line later
      Serial.printf("[SYS  ] [PB-E307] console: no memory for PROBOT_CONSOLE_BYTES=%u, using %u bytes"
                    " - docs: probotstudio.com/docs/hatalar/#pb-e307\n",
                    (unsigned)PROBOT_CONSOLE_BYTES, (unsigned)probot::console::detail::g_ring.cap);
    }

#ifdef ESP32
    probot::driverstation::start_driver_station();
#if PROBOT_BATTERY_ENABLED
    probot::io::battery::startTask();
#endif
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
