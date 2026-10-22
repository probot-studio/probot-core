#include "test_harness.hpp"

#include <probot/core/lifecycle.hpp>
#include <probot/core/loop_timing.hpp>

using probot::core::LoopPacer;

TEST_CASE(loop_pacer_keeps_a_fixed_rate_without_drift){
  LoopPacer p;
  p.start(100);
  bool overrun = true;
  EXPECT_TRUE(p.afterCycle(105, 20, false, &overrun) == 15 && !overrun);
  // Woke late at 123 and worked 3 ticks: the next start is still 140.
  EXPECT_TRUE(p.afterCycle(126, 20, false, &overrun) == 14 && !overrun);
  EXPECT_TRUE(p.afterCycle(159, 20, false, &overrun) == 1 && !overrun);
}

TEST_CASE(loop_pacer_counts_an_overrun_and_restarts_the_schedule){
  LoopPacer p;
  p.start(0);
  bool overrun = false;
  EXPECT_TRUE(p.afterCycle(75, 20, false, &overrun) == 1 && overrun);
  // No back-to-back catch-up passes: the next period starts after the yield.
  EXPECT_TRUE(p.afterCycle(80, 20, false, &overrun) == 16 && !overrun);
}

TEST_CASE(loop_pacer_ending_exactly_at_the_deadline_is_not_an_overrun){
  LoopPacer p;
  p.start(0);
  bool overrun = true;
  EXPECT_TRUE(p.afterCycle(20, 20, false, &overrun) == 1 && !overrun);
  EXPECT_TRUE(p.afterCycle(25, 20, false, &overrun) == 15 && !overrun);   // grid kept: 40
}

TEST_CASE(loop_pacer_does_not_count_a_late_one_shot_hook_pass){
  LoopPacer p;
  p.start(0);
  bool overrun = true;
  // teleopInit() took 300 ticks: late by design, still no catch-up passes.
  EXPECT_TRUE(p.afterCycle(300, 20, true, &overrun) == 1 && !overrun);
  EXPECT_TRUE(p.afterCycle(305, 20, false, &overrun) == 16 && !overrun);
  EXPECT_TRUE(p.afterCycle(400, 20, false, &overrun) == 1 && overrun);
}

TEST_CASE(loop_sleep_is_not_a_stall_but_a_hang_after_waking_is){
  using probot::robot::Phase;
  const uint32_t deadline = 500, period = 1000;
  // Cycle ends at t=100 and sleeps a full period, longer than the deadline.
  uint32_t hb = probot::core::wakeStampMs(100, period, 1);
  EXPECT_TRUE(!probot::core::isStalled(Phase::TELEOP_RUN, 100, hb, deadline));
  EXPECT_TRUE(!probot::core::isStalled(Phase::TELEOP_RUN, 1099, hb, deadline));
  // Woke at 1100 but the next pass never stamps (e.g. a hook hangs).
  EXPECT_TRUE(!probot::core::isStalled(Phase::TELEOP_RUN, 1100 + deadline, hb, deadline));
  EXPECT_TRUE(probot::core::isStalled(Phase::TELEOP_RUN, 1101 + deadline, hb, deadline));
  EXPECT_TRUE(probot::core::wakeStampMs(0xFFFFFFFFu, 1, 1) == 1u);   // 0 means "none"
}

TEST_CASE(loop_pacer_survives_tick_wraparound_and_period_changes){
  LoopPacer p;
  p.start(0xFFFFFFF0u);
  bool overrun = true;
  EXPECT_TRUE(p.afterCycle(0xFFFFFFF5u, 20, false, &overrun) == 15 && !overrun);  // next = 4
  EXPECT_TRUE(p.afterCycle(6u, 50, false, &overrun) == 48 && !overrun);           // next = 54
}

TEST_CASE(loop_period_setter_validates_and_restores_the_default){
  EXPECT_TRUE(probot::loopPeriodMs() == 20);
  EXPECT_TRUE(probot::setLoopPeriodMs(10));
  EXPECT_TRUE(probot::loopPeriodMs() == 10);
  EXPECT_TRUE(!probot::setLoopPeriodMs(1001));
  EXPECT_TRUE(probot::loopPeriodMs() == 10);
  EXPECT_TRUE(probot::setLoopPeriodMs(1) && probot::setLoopPeriodMs(1000));
  EXPECT_TRUE(probot::setLoopPeriodMs(0));
  EXPECT_TRUE(probot::loopPeriodMs() == 20);
  EXPECT_TRUE(probot::loopOverruns() == 0);
}
