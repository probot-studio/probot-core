#include "test_harness.hpp"

#include <probot/telemetry/telemetry.hpp>

#include <cstring>
#include <string>

namespace {
  void resetTelemetry() {
    probot::telemetry::clear();
  }
}

TEST_CASE(telemetry_basic_print){
  resetTelemetry();
  probot::telemetry::print("hello");
  EXPECT_TRUE(std::strcmp(probot::telemetry::getBuffer(), "hello") == 0);
  EXPECT_TRUE(probot::telemetry::getLength() == 5);
}

TEST_CASE(telemetry_println_appends_newline){
  resetTelemetry();
  probot::telemetry::println("a");
  probot::telemetry::println("b");
  EXPECT_TRUE(std::strcmp(probot::telemetry::getBuffer(), "a\nb\n") == 0);
}

TEST_CASE(telemetry_printf_formats){
  resetTelemetry();
  probot::telemetry::printf("x=%d y=%.1f", 7, 1.5);
  EXPECT_TRUE(std::strcmp(probot::telemetry::getBuffer(), "x=7 y=1.5") == 0);
}

TEST_CASE(telemetry_clear_empties_buffer){
  resetTelemetry();
  probot::telemetry::print("data");
  probot::telemetry::clear();
  EXPECT_TRUE(probot::telemetry::getLength() == 0);
  EXPECT_TRUE(std::strcmp(probot::telemetry::getBuffer(), "") == 0);
}

TEST_CASE(telemetry_overflow_keeps_most_recent){
  resetTelemetry();
  // Fill well past BUFFER_SIZE (256) with distinguishable chunks.
  for (int i = 0; i < 60; i++) {
    char chunk[16];
    snprintf(chunk, sizeof(chunk), "[%03d]", i);  // 5 bytes each, 300 total
    probot::telemetry::print(chunk);
  }
  const char* out = probot::telemetry::getBuffer();
  size_t len = probot::telemetry::getLength();
  EXPECT_TRUE(len == probot::telemetry::detail::BUFFER_SIZE);
  // The newest chunk must be the tail of the buffer.
  std::string s(out);
  EXPECT_TRUE(s.size() == len);
  EXPECT_TRUE(s.rfind("[059]") == s.size() - 5);
  // The oldest data must have been dropped.
  EXPECT_TRUE(s.find("[000]") == std::string::npos);
}

TEST_CASE(telemetry_single_message_larger_than_buffer){
  resetTelemetry();
  std::string big(400, 'x');
  big += "END";
  probot::telemetry::print(big.c_str());
  const char* out = probot::telemetry::getBuffer();
  size_t len = probot::telemetry::getLength();
  EXPECT_TRUE(len == probot::telemetry::detail::BUFFER_SIZE);
  std::string s(out);
  // Only the last BUFFER_SIZE bytes survive, ending with END.
  EXPECT_TRUE(s.rfind("END") == s.size() - 3);
}

TEST_CASE(telemetry_seq_increments){
  resetTelemetry();
  uint32_t s0 = probot::telemetry::getSeq();
  probot::telemetry::print("x");
  EXPECT_TRUE(probot::telemetry::getSeq() > s0);
}

TEST_CASE(telemetry_printf_is_not_cut_at_127_chars){
  resetTelemetry();
  std::string line(200, 'k');
  probot::printf("%s!", line.c_str());
  EXPECT_TRUE(std::string(probot::telemetry::getBuffer()) == line + "!");
}
