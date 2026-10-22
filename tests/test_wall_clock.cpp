#include "test_harness.hpp"

#include <probot/core/wall_clock.hpp>
#include <probot/ds.hpp>

namespace wc = probot::wallclock::detail;

namespace {
  void frameFor(int64_t ms, uint8_t* f) {
    f[0] = wc::kFrame;
    for (int i = 1; i < 9; ++i) { f[i] = (uint8_t)(ms & 0xFF); ms >>= 8; }
  }
}

TEST_CASE(wall_clock_frame_decoding_validates_network_input){
  const int64_t t = 1792656000123LL;               // 2026-10-22
  uint8_t f[10];
  frameFor(t, f);
  int64_t out = 0;
  EXPECT_TRUE(wc::decodeFrame(f, 9, out) && out == t);
  EXPECT_TRUE(!wc::decodeFrame(f, 8, out) && !wc::decodeFrame(f, 10, out) && !wc::decodeFrame(nullptr, 9, out));
  f[0] = 'J';
  EXPECT_TRUE(!wc::decodeFrame(f, 9, out));
  frameFor(1000, f);                               // 1970: a browser with no clock
  EXPECT_TRUE(!wc::decodeFrame(f, 9, out));
  frameFor(wc::kMaxUnixMs, f);
  EXPECT_TRUE(!wc::decodeFrame(f, 9, out));
  frameFor(-5, f);                                 // negative as i64
  EXPECT_TRUE(!wc::decodeFrame(f, 9, out));
}

TEST_CASE(wall_clock_range_edges_and_split){
  uint8_t f[9];
  int64_t out = 0;
  frameFor(wc::kMinUnixMs, f);
  EXPECT_TRUE(wc::decodeFrame(f, 9, out) && out == wc::kMinUnixMs);
  frameFor(wc::kMinUnixMs - 1, f);
  EXPECT_TRUE(!wc::decodeFrame(f, 9, out));
  frameFor(wc::kMaxUnixMs - 1, f);
  EXPECT_TRUE(wc::decodeFrame(f, 9, out));
  int64_t sec = 0;
  int32_t usec = 0;
  wc::split(1792656000123LL, sec, usec);
  EXPECT_TRUE(sec == 1792656000LL && usec == 123000);
  wc::split(wc::kMaxUnixMs - 1, sec, usec);
  EXPECT_TRUE(sec == 4102444799LL && usec == 999000);
}

TEST_CASE(wall_clock_first_time_always_later_ones_only_when_stopped){
  using P = probot::robot::Phase;
  using S = probot::robot::Status;
  wc::g_set = false;
  EXPECT_TRUE(!probot::ds::systemTimeValid());
  EXPECT_TRUE(!wc::setFromDs(5, P::STOPPED, S::STOP));                 // out of range
  EXPECT_TRUE(wc::setFromDs(1792656000000LL, P::TELEOP_RUN, S::START)); // first time, even mid-run
  EXPECT_TRUE(probot::ds::systemTimeValid());

  // A reconnect during a match keeps the timeline: only STOPPED with
  // nothing pending may move the clock.
  const P busy[] = {P::AUTO_INIT, P::AUTO_RUN, P::TELEOP_INIT, P::TELEOP_RUN, P::TRANSITION,
                     P::UTILITY_INIT, P::UTILITY_RUN};
  for (P ph : busy) EXPECT_TRUE(!wc::setFromDs(1792656060000LL, ph, S::STOP));
  EXPECT_TRUE(!wc::setFromDs(1792656060000LL, P::STOPPED, S::INIT));     // INIT accepted, not yet published
  EXPECT_TRUE(!wc::setFromDs(1792656060000LL, P::STOPPED, S::START));
  EXPECT_TRUE(wc::setFromDs(1792656060000LL, P::STOPPED, S::STOP));
  wc::g_set = false;
}
