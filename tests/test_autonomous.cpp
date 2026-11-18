#include "test_harness.hpp"

#include <probot/robot/autonomous.hpp>

#include <cstdio>
#include <cstring>

namespace {
  int left_init = 0;
  int left_periodic = 0;
  int right_init = 0;
  int right_periodic = 0;
  int legacy_init = 0;
  int legacy_periodic = 0;

  void leftInit() { ++left_init; }
  void leftPeriodic() { ++left_periodic; }
  void rightInit() { ++right_init; }
  void rightPeriodic() { ++right_periodic; }
  void legacyInit() { ++legacy_init; }
  void legacyPeriodic() { ++legacy_periodic; }
}

TEST_CASE(autonomous_registry_register_select_and_dispatch){
  auto& registry = probot::autonomous::registry();
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("left", "Soldan başla", leftInit, leftPeriodic, true));
  EXPECT_TRUE(registry.addRoutine("right", "Sağdan başla", rightInit, rightPeriodic));
  registry.finishRegistration();

  auto snap = registry.snapshot();
  EXPECT_TRUE(snap.count == 2);
  EXPECT_TRUE(snap.selected == 0);
  EXPECT_TRUE(strcmp(snap.routines[snap.selected].id, "left") == 0);
  EXPECT_TRUE(!registry.select("missing"));
  EXPECT_TRUE(registry.select("right"));
  EXPECT_TRUE(registry.beginRun("right"));
  EXPECT_TRUE(!registry.beginRun("left"));

  left_init = left_periodic = right_init = right_periodic = 0;
  registry.dispatchInit(legacyInit);
  registry.dispatchPeriodic(legacyPeriodic);
  EXPECT_TRUE(left_init == 0 && left_periodic == 0);
  EXPECT_TRUE(right_init == 1 && right_periodic == 1);
  EXPECT_TRUE(legacy_init == 0 && legacy_periodic == 0);
}

TEST_CASE(autonomous_registry_validates_ids_callbacks_defaults_and_capacity){
  auto& registry = probot::autonomous::registry();
  registry.beginRegistration();
  EXPECT_TRUE(!registry.addRoutine("Bad ID", "Invalid", leftInit, leftPeriodic));
  EXPECT_TRUE(!registry.addRoutine("missing-hooks", "Invalid", nullptr, leftPeriodic));
  EXPECT_TRUE(registry.addRoutine("one", "One", leftInit, leftPeriodic, true));
  EXPECT_TRUE(!registry.addRoutine("one", "Duplicate", rightInit, rightPeriodic));
  EXPECT_TRUE(!registry.addRoutine("two", "Second default", rightInit, rightPeriodic, true));

  char id[probot::autonomous::MAX_ID_LENGTH + 1];
  for (size_t i = 1; i < probot::autonomous::MAX_ROUTINES; ++i) {
    snprintf(id, sizeof(id), "mode-%u", static_cast<unsigned>(i));
    EXPECT_TRUE(registry.addRoutine(id, "Routine", leftInit, leftPeriodic));
  }
  EXPECT_TRUE(!registry.addRoutine("overflow", "Overflow", leftInit, leftPeriodic));
  registry.finishRegistration();
  EXPECT_TRUE(registry.registrationError());
  EXPECT_TRUE(registry.snapshot().count == probot::autonomous::MAX_ROUTINES);
  char json[3072];
  EXPECT_TRUE(probot::autonomous::buildModesJson(registry.snapshot(), json, sizeof(json)));
  EXPECT_TRUE(strstr(json, "\"registrationError\":true") != nullptr);
}

TEST_CASE(autonomous_registry_name_overload_uses_code_name_as_label){
  auto& registry = probot::autonomous::registry();
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("LeftAuto", leftInit, leftPeriodic, true));
  registry.finishRegistration();
  const auto snap = registry.snapshot();
  EXPECT_TRUE(strcmp(snap.routines[0].id, "LeftAuto") == 0);
  EXPECT_TRUE(strcmp(snap.routines[0].label, "LeftAuto") == 0);
}

TEST_CASE(autonomous_registry_uses_legacy_hooks_without_catalog){
  auto& registry = probot::autonomous::registry();
  registry.beginRegistration();
  registry.finishRegistration();
  legacy_init = legacy_periodic = 0;
  registry.dispatchInit(legacyInit);
  registry.dispatchPeriodic(legacyPeriodic);
  EXPECT_TRUE(legacy_init == 1);
  EXPECT_TRUE(legacy_periodic == 1);
}

TEST_CASE(autonomous_registry_requires_explicit_selection_without_default){
  auto& registry = probot::autonomous::registry();
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("left", "Left", leftInit, leftPeriodic));
  EXPECT_TRUE(registry.addRoutine("right", "Right", rightInit, rightPeriodic));
  registry.finishRegistration();
  EXPECT_TRUE(registry.snapshot().selected == -1);
  EXPECT_TRUE(!registry.beginRun("left"));
  EXPECT_TRUE(registry.select("left"));
  EXPECT_TRUE(registry.beginRun("left"));
}

TEST_CASE(autonomous_modes_json_escapes_labels_and_reports_selection){
  auto& registry = probot::autonomous::registry();
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("left", "Sol \"A\"\\test\nbaşlangıç", leftInit, leftPeriodic, true));
  registry.finishRegistration();
  char json[512];
  EXPECT_TRUE(probot::autonomous::buildModesJson(registry.snapshot(), json, sizeof(json)));
  EXPECT_TRUE(strstr(json, "\"selected\":\"left\"") != nullptr);
  EXPECT_TRUE(strstr(json, "Sol \\\"A\\\"\\\\test\\n") != nullptr);
  EXPECT_TRUE(strstr(json, "başlangıç") != nullptr);

  char tooSmall[8];
  EXPECT_TRUE(!probot::autonomous::buildModesJson(registry.snapshot(), tooSmall, sizeof(tooSmall)));
}
