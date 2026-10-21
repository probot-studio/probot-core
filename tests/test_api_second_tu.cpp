#include "test_harness.hpp"

// A second translation unit including the config-free API: links only if
// every header behind it is safe to include from several .cpp files.
#include <probot/api.hpp>

TEST_CASE(api_header_links_from_a_second_translation_unit){
  EXPECT_TRUE(&probot::io::gamepad() != &probot::io::rawGamepad());
  EXPECT_TRUE(&probot::robot::state() == &probot::robot::state());
  (void)probot::ds::packetSeq();
  (void)probot::framework::installed();
}
