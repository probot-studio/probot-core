#pragma once
#include <stdint.h>

// Framework registration: lets a layer such as a robot-framework SDK run its
// own code on the user task in every phase, around the user's OpMode hooks.
// Config-free: safe to include from any translation unit.
//
//   static const bool kInstalled = probot::framework::install({
//     &onBoot, &onPreCycle, &onPostCycle, &onEmergencyStop });
//
// Contract:
// - install() is accepted once, before runtime_setup() starts the tasks
//   (static initialization is fine); later calls return false.
// - boot runs once on the user task before the first cycle.
// - preCycle / postCycle run on every user-task iteration in ALL phases,
//   before / after the OpMode hook. In INIT and RUN they are covered by the
//   same stall deadline (PB-E301) as initLoop/loop.
//   In STOPPED and TRANSITION there is no stall deadline: a hang there is not
//   detected.
// - emergencyStop runs once the user task is deleted, on a fresh task on
//   core 1, before the active stop hook. Together with that hook it must
//   finish within PROBOT_ESTOP_END_MS or the chip reboots (PB-E305). Keep it
//   short and avoid locks user code may have held: put outputs in a safe
//   state and return.
// - Any field may be null.
// - Reference the registration from the sketch (or link with
//   lib_archive = no in PlatformIO): an archived library object that nothing
//   references can be dropped by the linker together with its static
//   initializer.
//
// A sketch that installs a framework defines PROBOT_FRAMEWORK before
// <probot.h>; the OpMode hooks then become optional (no PB-E201 link error
// when the framework provides the behavior).

namespace probot::framework {

  struct Hooks {
    void (*boot)();
    void (*preCycle)();
    void (*postCycle)();
    void (*emergencyStop)();
  };

  namespace detail {
    inline Hooks g_hooks{ nullptr, nullptr, nullptr, nullptr };
    inline volatile uint32_t g_installed = 0;
    inline volatile uint32_t g_started   = 0;
  }

  inline bool install(const Hooks& hooks) {
    if (__atomic_load_n(&detail::g_started, __ATOMIC_SEQ_CST)) return false;
    if (__atomic_exchange_n(&detail::g_installed, 1u, __ATOMIC_SEQ_CST)) return false;
    detail::g_hooks = hooks;
    return true;
  }

  inline bool installed() {
    return __atomic_load_n(&detail::g_installed, __ATOMIC_SEQ_CST) != 0;
  }

  // Called by runtime_setup() before the tasks start: freezes registration
  // and returns the hooks the runtime will use.
  inline Hooks seal() {
    __atomic_store_n(&detail::g_started, 1u, __ATOMIC_SEQ_CST);
    return detail::g_hooks;
  }

} // namespace probot::framework
