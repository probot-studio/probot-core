#include "test_harness.hpp"

#include <probot/robot/state.hpp>

TEST_CASE(state_service_updates_fields){
  probot::robot::StateService state;
  auto snap0 = state.read();
  EXPECT_TRUE(snap0.status == probot::robot::Status::STOP);
  EXPECT_TRUE(snap0.phase == probot::robot::Phase::STOPPED);
  EXPECT_TRUE(snap0.selectedMode == probot::robot::OpMode::TELEOP);

  state.setStatus(10u, probot::robot::Status::INIT);
  state.setPhase(10u, probot::robot::Phase::AUTO_INIT);
  state.setSelectedMode(10u, probot::robot::OpMode::AUTO);
  state.setBatteryVoltage(10u, 11.5f);
  state.setAutoPeriodSeconds(10u, 15);
  state.setAutoStartMs(10u, 123u);
  state.setClientCount(10u, 1);
  state.setDeadlineMiss(10u, true);

  auto snap = state.read();
  EXPECT_TRUE(snap.status == probot::robot::Status::INIT);
  EXPECT_TRUE(snap.phase == probot::robot::Phase::AUTO_INIT);
  EXPECT_TRUE(snap.selectedMode == probot::robot::OpMode::AUTO);
  EXPECT_NEAR(snap.batteryVoltage, 11.5f, 1e-5f);
  EXPECT_TRUE(snap.autoPeriodSeconds == 15);
  EXPECT_TRUE(snap.autoStartMs == 123u);
  EXPECT_TRUE(snap.clientCount == 1);
  EXPECT_TRUE(snap.deadlineMiss);
  EXPECT_TRUE(snap.seq > snap0.seq);

  const auto beforeTouch = snap.seq;
  state.touch(11u);
  EXPECT_TRUE(state.read().seq > beforeTouch);
}
