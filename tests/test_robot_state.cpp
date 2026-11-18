#include "test_harness.hpp"

#include <probot/robot/state.hpp>

TEST_CASE(state_service_updates_fields){
  probot::robot::StateService state;
  auto snap0 = state.read();
  EXPECT_TRUE(snap0.status == probot::robot::Status::STOP);
  EXPECT_TRUE(snap0.phase == probot::robot::Phase::NOT_INIT);

  state.setStatus(10u, probot::robot::Status::INIT);
  state.setPhase(10u, probot::robot::Phase::INITED);
  state.setBatteryVoltage(10u, 11.5f);
  state.setAutonomous(10u, true);
  state.setAutoPeriodSeconds(10u, 15);
  state.setAutoStartMs(10u, 123u);
  state.setClientCount(10u, 1);
  state.setDeadlineMiss(10u, true);

  auto snap = state.read();
  EXPECT_TRUE(snap.status == probot::robot::Status::INIT);
  EXPECT_TRUE(snap.phase == probot::robot::Phase::INITED);
  EXPECT_NEAR(snap.batteryVoltage, 11.5f, 1e-5f);
  EXPECT_TRUE(snap.autonomousEnabled);
  EXPECT_TRUE(snap.autoPeriodSeconds == 15);
  EXPECT_TRUE(snap.autoStartMs == 123u);
  EXPECT_TRUE(snap.clientCount == 1);
  EXPECT_TRUE(snap.deadlineMiss);
  EXPECT_TRUE(snap.seq > snap0.seq);

  const auto beforeTouch = snap.seq;
  state.touch(11u);
  EXPECT_TRUE(state.read().seq > beforeTouch);
}
