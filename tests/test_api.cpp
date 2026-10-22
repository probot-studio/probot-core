#include "test_harness.hpp"

// Compiles with no PROBOT_WIFI_* or battery macros: api.hpp is config-free.
#include <probot/api.hpp>

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
  probot::io::GamepadSnapshot out{};
  EXPECT_TRUE(!probot::ds::rawGamepad(1, out));

  float axes[2] = { 0.5f, -0.25f };
  bool buttons[1] = { true };
  _test_millis_now = 10;
  probot::io::rawGamepad().write(10u, axes, 2, buttons, 1);
  // Raw input is readable while STOPPED, unlike the gated gamepad.
  EXPECT_TRUE(probot::ds::rawGamepad(0, out));
  EXPECT_NEAR(out.axes[0], 0.5f, 1e-6f);
  EXPECT_TRUE(out.buttons[0]);
  EXPECT_TRUE(!probot::ds::rawGamepad(1, out));
  _test_millis_now = 0;
}
