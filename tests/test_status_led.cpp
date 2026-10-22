#include "test_harness.hpp"

#include <probot/core/status_led.hpp>

namespace robot = probot::robot;
namespace core  = probot::core;

namespace {
  robot::StateSnapshot snap(robot::Phase phase, int32_t clients, bool stalled = false) {
    robot::StateSnapshot s{};
    s.status = robot::Status::STOP;
    s.phase = phase;
    s.clientCount = clients;
    s.deadlineMiss = stalled;
    return s;
  }
  constexpr core::Rgb BLUE{0, 0, 255}, ORANGE{255, 128, 0}, GREEN{0, 255, 0},
                      YELLOW{255, 255, 0}, RED{255, 0, 0}, OFF{0, 0, 0};
}

// A running robot must show its run color even when no Driver Station is
// connected (tab closed, or before the DS timeout fires).
TEST_CASE(status_led_shows_run_phase_without_ds_client){
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::AUTO_RUN, 0), false, true) == ORANGE);
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::TELEOP_RUN, 0), false, true) == GREEN);
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::TELEOP_INIT, 0), false, false) == YELLOW);
}

TEST_CASE(status_led_idle_and_fault_colors){
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::STOPPED, 0), false, false) == BLUE); // solid
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::STOPPED, 1), false, true) == BLUE);
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::STOPPED, 1), false, false) == OFF);  // blinking
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::AUTO_RUN, 1, true), false, true) == RED);
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::AUTO_RUN, 1, true), false, false) == OFF);
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::TELEOP_RUN, 1), true, false) == RED);
}

TEST_CASE(rsl_blinks_only_in_run_phases){
  EXPECT_TRUE(core::rslBlinking(robot::Phase::AUTO_RUN, false));
  EXPECT_TRUE(core::rslBlinking(robot::Phase::TELEOP_RUN, false));
  EXPECT_TRUE(!core::rslBlinking(robot::Phase::TELEOP_RUN, true));
  EXPECT_TRUE(!core::rslBlinking(robot::Phase::AUTO_INIT, false));
  EXPECT_TRUE(!core::rslBlinking(robot::Phase::STOPPED, false));
}

TEST_CASE(active_phase_and_mode_classification){
  EXPECT_TRUE(core::isActivePhase(robot::Phase::AUTO_INIT));
  EXPECT_TRUE(core::isActivePhase(robot::Phase::TELEOP_RUN));
  EXPECT_TRUE(!core::isActivePhase(robot::Phase::STOPPED));
  EXPECT_TRUE(!core::isActivePhase(robot::Phase::TRANSITION));
  EXPECT_TRUE(core::modeOfPhase(robot::Phase::AUTO_INIT) == robot::OpMode::AUTO);
  EXPECT_TRUE(core::modeOfPhase(robot::Phase::AUTO_RUN) == robot::OpMode::AUTO);
  EXPECT_TRUE(core::modeOfPhase(robot::Phase::TELEOP_INIT) == robot::OpMode::TELEOP);
  EXPECT_TRUE(core::modeOfPhase(robot::Phase::TELEOP_RUN) == robot::OpMode::TELEOP);
}

TEST_CASE(status_led_and_rsl_cover_utility){
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::UTILITY_INIT, 1), false, false) == YELLOW);
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::UTILITY_RUN, 1), false, true) == (core::Rgb{160, 0, 255}));
  EXPECT_TRUE(core::statusColor(snap(robot::Phase::UTILITY_RUN, 1), false, false) == OFF);
  EXPECT_TRUE(core::rslBlinking(robot::Phase::UTILITY_RUN, false));
  EXPECT_TRUE(!core::rslBlinking(robot::Phase::UTILITY_INIT, false));
}
