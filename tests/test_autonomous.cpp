#include "test_harness.hpp"

#include <probot/core/lifecycle.hpp>
#include <probot/robot/autonomous.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace {
  namespace pa = probot::autonomous;
  using probot::robot::Phase;
  using probot::robot::Status;

  int left_start = 0;
  int left_loop = 0;
  int right_start = 0;
  int right_loop = 0;
  int legacy_start = 0;
  int legacy_loop = 0;

  void leftStart() { ++left_start; }
  void leftLoop() { ++left_loop; }
  void rightStart() { ++right_start; }
  void rightLoop() { ++right_loop; }
  void legacyStart() { ++legacy_start; }
  void legacyLoop() { ++legacy_loop; }

  void resetCounters() {
    left_start = left_loop = right_start = right_loop = legacy_start = legacy_loop = 0;
  }

  // Two-routine catalog, "left" is the default.
  pa::Registry& leftRightCatalog() {
    auto& registry = pa::registry();
    registry.beginRegistration();
    EXPECT_TRUE(registry.addRoutine("left", "Soldan başla", leftStart, leftLoop, true));
    EXPECT_TRUE(registry.addRoutine("right", "Sağdan başla", rightStart, rightLoop));
    registry.finishRegistration();
    return registry;
  }

  std::string modesJson() {
    char json[pa::MODES_JSON_CAPACITY];
    EXPECT_TRUE(pa::buildModesJson(pa::snapshot(), json, sizeof(json)));
    return json;
  }

  bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
  }
}

TEST_CASE(autonomous_registry_register_select_and_dispatch){
  auto& registry = leftRightCatalog();
  auto snap = registry.snapshot();
  EXPECT_TRUE(snap.count == 2);
  EXPECT_TRUE(strcmp(snap.selectedId(), "left") == 0);
  EXPECT_TRUE(snap.activeId() == nullptr);
  EXPECT_TRUE(!registry.select("missing"));
  EXPECT_TRUE(registry.select("right"));
  EXPECT_TRUE(registry.beginRun("right") == pa::RunCheck::OK);
  EXPECT_TRUE(strcmp(registry.snapshot().activeId(), "right") == 0);

  resetCounters();
  registry.dispatchStart(legacyStart);
  registry.dispatchLoop(legacyLoop);
  registry.dispatchLoop(legacyLoop);
  EXPECT_TRUE(left_start == 0 && left_loop == 0);
  EXPECT_TRUE(right_start == 1 && right_loop == 2);
  EXPECT_TRUE(legacy_start == 0 && legacy_loop == 0);
}

TEST_CASE(autonomous_selection_after_start_does_not_swap_the_running_routine){
  auto& registry = leftRightCatalog();
  EXPECT_TRUE(registry.beginRun("left") == pa::RunCheck::OK);
  EXPECT_TRUE(registry.select("right"));
  resetCounters();
  registry.dispatchLoop(legacyLoop);
  EXPECT_TRUE(left_loop == 1 && right_loop == 0);
}

TEST_CASE(autonomous_start_gate_reports_why_it_refused){
  auto& registry = leftRightCatalog();
  // The page must send the id it shows; a missing or stale one is refused.
  EXPECT_TRUE(registry.beginRun(nullptr) == pa::RunCheck::MISMATCH);
  EXPECT_TRUE(registry.beginRun("") == pa::RunCheck::MISMATCH);
  EXPECT_TRUE(registry.beginRun("right") == pa::RunCheck::MISMATCH);
  EXPECT_TRUE(registry.snapshot().activeId() == nullptr);

  // No default and nothing selected yet.
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("left", "Left", leftStart, leftLoop));
  EXPECT_TRUE(registry.addRoutine("right", "Right", rightStart, rightLoop));
  registry.finishRegistration();
  EXPECT_TRUE(registry.snapshot().selected == -1);
  EXPECT_TRUE(registry.beginRun("left") == pa::RunCheck::NO_SELECTION);
  EXPECT_TRUE(registry.select("left"));
  EXPECT_TRUE(registry.beginRun("left") == pa::RunCheck::OK);
}

TEST_CASE(autonomous_end_run_clears_the_active_routine){
  auto& registry = leftRightCatalog();
  EXPECT_TRUE(registry.beginRun("left") == pa::RunCheck::OK);
  registry.endRun();
  EXPECT_TRUE(registry.snapshot().activeId() == nullptr);
  resetCounters();
  // With a catalog in use nothing runs without a latched routine: neither a
  // stale routine nor the sketch's own autonomous hooks.
  registry.dispatchStart(legacyStart);
  registry.dispatchLoop(legacyLoop);
  EXPECT_TRUE(left_start == 0 && left_loop == 0 && right_start == 0 && right_loop == 0);
  EXPECT_TRUE(legacy_start == 0 && legacy_loop == 0);
  EXPECT_TRUE(contains(modesJson(), "\"active\":null"));
}

TEST_CASE(autonomous_refused_start_clears_a_previous_latch){
  auto& registry = leftRightCatalog();
  EXPECT_TRUE(registry.beginRun("left") == pa::RunCheck::OK);
  EXPECT_TRUE(registry.beginRun("right") == pa::RunCheck::MISMATCH);
  EXPECT_TRUE(registry.snapshot().activeId() == nullptr);
}

TEST_CASE(autonomous_restore_selection_keeps_default_for_unknown_ids){
  auto& registry = leftRightCatalog();
  EXPECT_TRUE(registry.restoreSelection("right"));
  EXPECT_TRUE(strcmp(registry.snapshot().selectedId(), "right") == 0);

  // A stored id from a different sketch, or a corrupt value, keeps the default.
  leftRightCatalog();
  EXPECT_TRUE(!registry.restoreSelection("gone"));
  EXPECT_TRUE(!registry.restoreSelection("bad id"));
  EXPECT_TRUE(!registry.restoreSelection(nullptr));
  EXPECT_TRUE(strcmp(registry.snapshot().selectedId(), "left") == 0);
}

TEST_CASE(autonomous_registration_error_blocks_selection_restore_and_start){
  auto& registry = pa::registry();
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("left", "Left", leftStart, leftLoop, true));
  EXPECT_TRUE(!registry.addRoutine("left", "Again", rightStart, rightLoop));
  EXPECT_TRUE(registry.addRoutine("right", "Right", rightStart, rightLoop));
  registry.finishRegistration();

  EXPECT_TRUE(registry.inUse());
  EXPECT_TRUE(registry.registrationError());
  EXPECT_TRUE(!registry.select("right"));
  EXPECT_TRUE(!registry.restoreSelection("right"));
  EXPECT_TRUE(registry.beginRun("left") == pa::RunCheck::CATALOG_ERROR);
  resetCounters();
  registry.dispatchStart(legacyStart);
  registry.dispatchLoop(legacyLoop);
  EXPECT_TRUE(left_start == 0 && left_loop == 0 && legacy_start == 0 && legacy_loop == 0);
}

TEST_CASE(autonomous_registry_records_the_first_error_and_its_id){
  struct Case { const char* id; const char* label; bool hooks; bool isDefault; const char* code; };
  const Case cases[] = {
    {"Bad ID", "Label", true, false, "invalid_id"},
    {"ThisIdIsTooLong1", "Label", true, false, "invalid_id"},
    {"Kırmızı", "Label", true, false, "invalid_id"},
    {"ok", "", true, false, "invalid_label"},
    {"ok", "Two\nlines", true, false, "invalid_label"},
    {"ok", "Label", false, false, "missing_callback"},
    {"left", "Duplicate", true, false, "duplicate_id"},
    {"other", "Second default", true, true, "multiple_defaults"},
  };
  for (const Case& c : cases) {
    auto& registry = pa::registry();
    registry.beginRegistration();
    EXPECT_TRUE(registry.addRoutine("left", "Left", leftStart, leftLoop, true));
    EXPECT_TRUE(!registry.addRoutine(c.id, c.label, c.hooks ? rightStart : nullptr, rightLoop, c.isDefault));
    // A later, different mistake does not overwrite the first one.
    EXPECT_TRUE(!registry.addRoutine("x y", "Label", rightStart, rightLoop));
    registry.finishRegistration();

    const std::string json = modesJson();
    const std::string expected = std::string("\"error\":{\"code\":\"") + c.code + "\",\"id\":\"" + c.id + "\"}";
    EXPECT_TRUE(contains(json, "\"registrationError\":true"));
    EXPECT_TRUE(contains(json, expected.c_str()));
  }
}

TEST_CASE(autonomous_registry_rejects_the_ninth_routine){
  auto& registry = pa::registry();
  registry.beginRegistration();
  char id[pa::MAX_ID_LENGTH + 1];
  for (size_t i = 0; i < pa::MAX_ROUTINES; ++i) {
    snprintf(id, sizeof(id), "mode-%u", static_cast<unsigned>(i));
    EXPECT_TRUE(registry.addRoutine(id, "Routine", leftStart, leftLoop, i == 0));
  }
  EXPECT_TRUE(!registry.addRoutine("overflow", "Overflow", leftStart, leftLoop));
  registry.finishRegistration();
  EXPECT_TRUE(registry.snapshot().count == pa::MAX_ROUTINES);
  EXPECT_TRUE(contains(modesJson(), "{\"code\":\"too_many_routines\",\"id\":\"overflow\"}"));
}

TEST_CASE(autonomous_error_id_is_sanitized_and_cut_on_a_utf8_boundary){
  auto& registry = pa::registry();
  // 30 ASCII bytes, then a two-byte 'ş' that would straddle the 31-byte limit.
  std::string longId(30, 'a');
  longId += "\xC5\x9F" "tail";
  registry.beginRegistration();
  EXPECT_TRUE(!registry.addRoutine(longId.c_str(), "Label", leftStart, leftLoop));
  registry.finishRegistration();
  EXPECT_TRUE(strcmp(registry.snapshot().errorId, std::string(30, 'a').c_str()) == 0);

  registry.beginRegistration();
  EXPECT_TRUE(!registry.addRoutine("a\tb\"c", "Label", leftStart, leftLoop));
  registry.finishRegistration();
  EXPECT_TRUE(strcmp(registry.snapshot().errorId, "a?b\"c") == 0);
  EXPECT_TRUE(contains(modesJson(), "\"id\":\"a?b\\\"c\""));

  registry.beginRegistration();
  EXPECT_TRUE(!registry.addRoutine(nullptr, "Label", leftStart, leftLoop));
  registry.finishRegistration();
  EXPECT_TRUE(contains(modesJson(), "{\"code\":\"invalid_id\",\"id\":\"\"}"));
}

TEST_CASE(autonomous_registry_name_overload_uses_the_id_as_label){
  auto& registry = pa::registry();
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("LeftAuto", leftStart, leftLoop, true));
  EXPECT_TRUE(!registry.addRoutine("Sol Otonom", rightStart, rightLoop));
  registry.finishRegistration();
  const auto snap = registry.snapshot();
  EXPECT_TRUE(strcmp(snap.routines[0].id, "LeftAuto") == 0);
  EXPECT_TRUE(strcmp(snap.routines[0].label, "LeftAuto") == 0);
  EXPECT_TRUE(contains(modesJson(), "{\"code\":\"invalid_id\",\"id\":\"Sol Otonom\"}"));
}

TEST_CASE(autonomous_registry_uses_legacy_hooks_without_catalog){
  auto& registry = pa::registry();
  registry.beginRegistration();
  registry.finishRegistration();
  EXPECT_TRUE(!registry.inUse());
  EXPECT_TRUE(registry.beginRun(nullptr) == pa::RunCheck::NO_SELECTION);
  resetCounters();
  registry.dispatchStart(legacyStart);
  registry.dispatchLoop(legacyLoop);
  EXPECT_TRUE(legacy_start == 1 && legacy_loop == 1);
  EXPECT_TRUE(contains(modesJson(), "\"supported\":false"));
}

TEST_CASE(autonomous_failed_catalog_without_routines_does_not_fall_back_to_legacy_hooks){
  auto& registry = pa::registry();
  registry.beginRegistration();
  EXPECT_TRUE(!registry.addRoutine("bad id", "Label", leftStart, leftLoop));
  registry.finishRegistration();
  EXPECT_TRUE(registry.inUse());
  resetCounters();
  registry.dispatchStart(legacyStart);
  registry.dispatchLoop(legacyLoop);
  EXPECT_TRUE(legacy_start == 0 && legacy_loop == 0);
  EXPECT_TRUE(contains(modesJson(), "\"supported\":true"));
}

TEST_CASE(autonomous_modes_json_escapes_labels_and_reports_selection){
  auto& registry = pa::registry();
  registry.beginRegistration();
  EXPECT_TRUE(registry.addRoutine("left", "Sol \"A\"\\test başlangıç", leftStart, leftLoop, true));
  registry.finishRegistration();
  const std::string json = modesJson();
  EXPECT_TRUE(contains(json, "\"selected\":\"left\""));
  EXPECT_TRUE(contains(json, "\"active\":null"));
  EXPECT_TRUE(contains(json, "\"error\":null"));
  EXPECT_TRUE(contains(json, "Sol \\\"A\\\"\\\\test başlangıç"));
  EXPECT_TRUE(contains(json, "\"default\":true"));

  char tooSmall[8];
  EXPECT_TRUE(!pa::buildModesJson(pa::snapshot(), tooSmall, sizeof(tooSmall)));
}

TEST_CASE(autonomous_json_escape_handles_control_characters){
  char out[32];
  size_t used = 0;
  EXPECT_TRUE(pa::appendJsonString(out, sizeof(out), used, "a\nb\x01"));
  out[used] = '\0';
  EXPECT_TRUE(strcmp(out, "\"a\\u000ab\\u0001\"") == 0);
}

TEST_CASE(autonomous_worst_case_catalog_fits_the_modes_json_capacity){
  // Every label and the error id made of characters that need escaping.
  const std::string label(pa::MAX_LABEL_LENGTH, '"');
  auto& registry = pa::registry();
  registry.beginRegistration();
  char id[pa::MAX_ID_LENGTH + 1];
  for (size_t i = 0; i < pa::MAX_ROUTINES; ++i) {
    snprintf(id, sizeof(id), "%0*u", static_cast<int>(pa::MAX_ID_LENGTH), static_cast<unsigned>(i));
    EXPECT_TRUE(registry.addRoutine(id, label.c_str(), leftStart, leftLoop, i == 0));
  }
  const std::string badId(pa::MAX_LABEL_LENGTH, '\\');
  EXPECT_TRUE(!registry.addRoutine(badId.c_str(), "x", leftStart, leftLoop));
  registry.finishRegistration();
  EXPECT_TRUE(registry.beginRun(nullptr) == pa::RunCheck::CATALOG_ERROR);
  char json[pa::MODES_JSON_CAPACITY];
  EXPECT_TRUE(pa::buildModesJson(pa::snapshot(), json, sizeof(json)));
}

TEST_CASE(autonomous_selection_is_accepted_only_outside_a_run){
  using probot::core::canAcceptAutoSelect;
  EXPECT_TRUE(canAcceptAutoSelect(Phase::STOPPED, Status::STOP));
  EXPECT_TRUE(canAcceptAutoSelect(Phase::TRANSITION, Status::STOP));
  EXPECT_TRUE(canAcceptAutoSelect(Phase::AUTO_INIT, Status::INIT));
  EXPECT_TRUE(canAcceptAutoSelect(Phase::TELEOP_INIT, Status::INIT));
  // START accepted but not yet observed by the user task: the run is committed.
  EXPECT_TRUE(!canAcceptAutoSelect(Phase::AUTO_INIT, Status::START));
  EXPECT_TRUE(!canAcceptAutoSelect(Phase::AUTO_RUN, Status::START));
  EXPECT_TRUE(!canAcceptAutoSelect(Phase::TELEOP_RUN, Status::START));
  // A STOP request still pending while the run phase is visible.
  EXPECT_TRUE(!canAcceptAutoSelect(Phase::AUTO_RUN, Status::STOP));
}
