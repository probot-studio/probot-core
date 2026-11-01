#include "test_harness.hpp"

#include <probot/version.hpp>

#include <cstdio>
#include <cstring>

#if !defined(PROBOT_VERSION_MAJOR) || !defined(PROBOT_VERSION_MINOR) || !defined(PROBOT_VERSION_PATCH)
#error "version.hpp must provide PROBOT_VERSION_* macros"
#endif
static_assert(PROBOT_VERSION == PROBOT_VERSION_OF(PROBOT_VERSION_MAJOR, PROBOT_VERSION_MINOR, PROBOT_VERSION_PATCH),
              "PROBOT_VERSION must combine the parts");

// The check documented for generated sketches: a plain number, so it also
// preprocesses on 0.3.x, where neither PROBOT_VERSION nor PROBOT_VERSION_OF exists.
#if !defined(PROBOT_VERSION) || PROBOT_VERSION < 400
#error "the documented 0.4 check must pass on this version"
#endif

TEST_CASE(version_string_starts_with_the_numeric_parts){
  char parts[32];
  std::snprintf(parts, sizeof(parts), "%d.%d.%d", PROBOT_VERSION_MAJOR, PROBOT_VERSION_MINOR, PROBOT_VERSION_PATCH);
  const size_t n = std::strlen(parts);
  EXPECT_TRUE(std::strncmp(PROBOT_VERSION_STRING, parts, n) == 0);
  const char next = PROBOT_VERSION_STRING[n];
  EXPECT_TRUE(next == '\0' || next == '-' || next == '+');
}

TEST_CASE(version_numbers_order_like_semver){
  EXPECT_TRUE(PROBOT_VERSION_OF(0, 4, 0) == 400);
  EXPECT_TRUE(PROBOT_VERSION_OF(0, 3, 9) < PROBOT_VERSION_OF(0, 4, 0));
  EXPECT_TRUE(PROBOT_VERSION_OF(0, 4, 99) < PROBOT_VERSION_OF(0, 5, 0));
  EXPECT_TRUE(PROBOT_VERSION_OF(0, 99, 99) < PROBOT_VERSION_OF(1, 0, 0));
}
