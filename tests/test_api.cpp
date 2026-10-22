#include "test_harness.hpp"

// Compiles with no PROBOT_WIFI_* or battery macros: api.hpp is config-free.
#include <probot/api.hpp>
#include "gamepad_test_util.hpp"

extern unsigned long _test_millis_now;

#if !defined(PROBOT_VERSION_MAJOR) || !defined(PROBOT_VERSION_MINOR) || !defined(PROBOT_VERSION_PATCH)
#error "api.hpp must provide PROBOT_VERSION_* macros"
#endif
static_assert(PROBOT_VERSION == PROBOT_VERSION_OF(PROBOT_VERSION_MAJOR, PROBOT_VERSION_MINOR, PROBOT_VERSION_PATCH),
              "PROBOT_VERSION must combine the parts");

namespace {
  int g_boots = 0;
  void onBoot(){ ++g_boots; }

  void resetFramework(){
    probot::framework::detail::g_hooks = probot::framework::Hooks{ nullptr, nullptr, nullptr, nullptr };
    probot::framework::detail::g_installed = 0;
    probot::framework::detail::g_started = 0;
  }

  void resetDs(){
    using namespace probot::robot;
    g_estop_latched = 0;
    g_ds_packet_seq = 0;
    auto& s = state();
    s.setControl(0u, Status::STOP, OpMode::TELEOP);
    s.setPhase(0u, Phase::STOPPED);
    s.setClientCount(0u, 0);
    s.setDeadlineMiss(0u, false);
    s.setAutoStartMs(0u, 0u);
    s.setAutoPeriodSeconds(0u, 30);
  }
}

TEST_CASE(framework_install_is_accepted_once_before_seal){
  resetFramework();
  EXPECT_TRUE(!probot::framework::installed());
  EXPECT_TRUE(probot::framework::install({ &onBoot, nullptr, nullptr, nullptr }));
  EXPECT_TRUE(probot::framework::installed());
  EXPECT_TRUE(!probot::framework::install({ nullptr, nullptr, nullptr, nullptr }));

  auto sealed = probot::framework::seal();
  EXPECT_TRUE(sealed.boot == &onBoot);
  EXPECT_TRUE(sealed.preCycle == nullptr);
  sealed.boot();
  EXPECT_TRUE(g_boots == 1);
  resetFramework();
}

TEST_CASE(framework_install_after_seal_is_rejected){
  resetFramework();
  auto sealed = probot::framework::seal();
  EXPECT_TRUE(sealed.boot == nullptr);
  EXPECT_TRUE(!probot::framework::install({ &onBoot, nullptr, nullptr, nullptr }));
  EXPECT_TRUE(!probot::framework::installed());
  resetFramework();
}

TEST_CASE(ds_outputs_enabled_only_in_started_run_without_estop){
  using namespace probot::robot;
  resetDs();
  auto& s = state();
  EXPECT_TRUE(!probot::ds::outputsEnabled());

  s.setControl(1u, Status::START, OpMode::TELEOP);
  s.setPhase(1u, Phase::TELEOP_RUN);
  EXPECT_TRUE(probot::ds::outputsEnabled());
  EXPECT_TRUE(probot::ds::phase() == Phase::TELEOP_RUN);

  s.setDeadlineMiss(2u, true);
  EXPECT_TRUE(!probot::ds::outputsEnabled());
  s.setDeadlineMiss(3u, false);

  g_estop_latched = 1;
  EXPECT_TRUE(probot::ds::isEStopped());
  EXPECT_TRUE(!probot::ds::outputsEnabled());
  EXPECT_TRUE(probot::ds::phase() == Phase::STOPPED);
  resetDs();
}

TEST_CASE(ds_match_time_counts_down_only_in_auto_run){
  using namespace probot::robot;
  resetDs();
  auto& s = state();
  _test_millis_now = 6000;
  EXPECT_TRUE(probot::ds::matchTimeSeconds() < 0.0f);

  s.setPhase(1u, Phase::AUTO_RUN);
  s.setAutoPeriodSeconds(1u, 15);
  s.setAutoStartMs(1u, 1000u);
  EXPECT_NEAR(probot::ds::matchTimeSeconds(), 10.0f, 1e-3f);

  _test_millis_now = 20000;
  EXPECT_NEAR(probot::ds::matchTimeSeconds(), 0.0f, 1e-6f);

  s.setPhase(2u, Phase::TELEOP_RUN);
  EXPECT_TRUE(probot::ds::matchTimeSeconds() < 0.0f);
  _test_millis_now = 0;
  resetDs();
}

TEST_CASE(ds_attached_and_packet_seq){
  using namespace probot::robot;
  resetDs();
  EXPECT_TRUE(!probot::ds::isAttached());
  state().setClientCount(1u, 1);
  EXPECT_TRUE(probot::ds::isAttached());

  EXPECT_TRUE(probot::ds::packetSeq() == 0u);
  noteDsPacket();
  noteDsPacket();
  EXPECT_TRUE(probot::ds::packetSeq() == 2u);
  resetDs();
}

TEST_CASE(ds_raw_gamepad_is_ungated_and_reports_missing_frames){
  resetGamepadsForTest();
  probot::io::GamepadSnapshot out{};
  EXPECT_TRUE(!probot::ds::rawGamepad(1, out));

  float axes[2] = { 0.5f, -0.25f };
  bool buttons[1] = { true };
  _test_millis_now = 10;
  probot::io::detail::applyFrame(0, 10u, false, axes, 2, buttons, 1);
  // Raw input is readable while STOPPED, unlike the gated gamepad.
  EXPECT_TRUE(probot::ds::rawGamepad(0, out));
  EXPECT_NEAR(out.axes[0], 0.5f, 1e-6f);
  EXPECT_TRUE(out.buttons[0]);
  EXPECT_TRUE(!probot::ds::rawGamepad(1, out));
  _test_millis_now = 0;
}

TEST_CASE(request_stop_is_a_cooperative_stop_from_code){
  using namespace probot::robot;
  resetDs();
  auto& s = state();
  uint32_t seq = s.read().seq;
  EXPECT_TRUE(!probot::requestStop());                 // idle: nothing to stop
  EXPECT_TRUE(s.read().seq == seq);                    // and nothing published

  s.setControl(1u, Status::START, OpMode::TELEOP);
  s.setPhase(1u, Phase::TELEOP_RUN);
  EXPECT_TRUE(probot::requestStop());
  EXPECT_TRUE(s.status() == Status::STOP);
  EXPECT_TRUE(s.phase() == Phase::TELEOP_RUN);         // the user task publishes the phase
  EXPECT_TRUE(!probot::ds::outputsEnabled());

  // An accepted INIT not yet published as a phase is cancelled too.
  resetDs();
  s.setStatus(2u, Status::INIT);
  EXPECT_TRUE(probot::requestStop());
  EXPECT_TRUE(s.status() == Status::STOP);

  // A stop in progress (status STOP, phase still active) is not a new one,
  // nor is the window after autonomous expiry.
  s.setPhase(3u, Phase::TELEOP_RUN);
  EXPECT_TRUE(!probot::requestStop());
  s.setPhase(3u, Phase::TRANSITION);
  EXPECT_TRUE(!probot::requestStop());

  // After the emergency stop latches there is nothing left to stop.
  s.setControl(3u, Status::START, OpMode::AUTO);
  s.setPhase(3u, Phase::AUTO_RUN);
  g_estop_latched = 1;
  EXPECT_TRUE(!probot::requestStop());
  EXPECT_TRUE(s.status() == Status::START);
  resetDs();
}

namespace {
  int g_rsLoops = 0, g_rsStops = 0;
  void rsLoop(){ if (++g_rsLoops == 2) EXPECT_TRUE(probot::requestStop()); }
  void rsStop(){ ++g_rsStops; }
}

// The contract the docs promise: a stop requested from a hook never
// interrupts it, runs the stop hook once and leaves the robot idle.
TEST_CASE(request_stop_from_a_loop_hook_ends_the_run_cooperatively){
  using namespace probot::robot;
  namespace core = probot::core;
  resetDs();
  g_rsLoops = g_rsStops = 0;
  auto& rs = state();
  core::Hooks h{};
  h.teleopLoop = rsLoop;
  h.teleopStop = rsStop;
  core::PhaseMachine m;
  core::Supervisor sup;
  rs.setControl(1u, Status::INIT, OpMode::TELEOP);
  for (uint32_t t = 10; t < 30; t += 10) m.step(h, sup.update(rs, t, false), rs, t);
  rs.setStatus(30u, Status::START);
  for (uint32_t t = 30; t < 100; t += 10) m.step(h, sup.update(rs, t, false), rs, t);
  EXPECT_TRUE(g_rsLoops == 2);
  EXPECT_TRUE(g_rsStops == 1);
  EXPECT_TRUE(rs.phase() == Phase::STOPPED);
  EXPECT_TRUE(core::canAcceptInit(rs.phase(), rs.status()));
  EXPECT_TRUE(!probot::requestStop());
  resetDs();
}

TEST_CASE(ds_rsl_state_and_comms_disable_count_read_the_runtime_counters){
  using namespace probot::robot;
  g_rsl_on = 0;
  g_comms_disable_count = 0;
  EXPECT_TRUE(!probot::ds::rslState());
  EXPECT_TRUE(probot::ds::commsDisableCount() == 0u);
  g_rsl_on = 1;
  g_comms_disable_count = 3;
  EXPECT_TRUE(probot::ds::rslState());
  EXPECT_TRUE(probot::ds::commsDisableCount() == 3u);
  g_rsl_on = 0;
  g_comms_disable_count = 0;
}
