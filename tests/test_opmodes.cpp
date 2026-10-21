#include "test_harness.hpp"

#include <probot/ds.hpp>
#include <probot/robot/opmodes.hpp>

#include <string>

namespace robot = probot::robot;
using robot::OpMode;
using robot::OpModeOption;

namespace {
  std::string readAll(const robot::OpModeRegistry& reg, size_t chunk) {
    std::string out;
    char buf[64];
    const uint32_t ver = reg.version();
    bool stale = false;
    for (size_t off = 0;;) {
      size_t n = reg.readJson(ver, off, buf, chunk < sizeof buf ? chunk : sizeof buf, &stale);
      if (stale || !n) break;
      out.append(buf, n);
      off += n;
    }
    return out;
  }

  void resetAll() {
    robot::opModes().resetForTest();
    auto& rs = robot::state();
    rs.setControl(0u, robot::Status::STOP, OpMode::TELEOP, 0);
    rs.setPhase(0u, robot::Phase::STOPPED);
    __atomic_store_n(&robot::g_utility_hooks, 0u, __ATOMIC_SEQ_CST);
  }
}

TEST_CASE(opmodes_reject_invalid_lists_and_keep_the_old_one){
  robot::OpModeRegistry reg;
  const OpModeOption good[] = {{1, OpMode::AUTO, "A", nullptr, nullptr}};
  EXPECT_TRUE(reg.set(good, 1));
  const uint32_t v = reg.version();

  const OpModeOption zeroId[] = {{0, OpMode::AUTO, "A", "", ""}};
  const OpModeOption dupId[] = {{5, OpMode::AUTO, "A", "", ""}, {5, OpMode::TELEOP, "B", "", ""}};
  const OpModeOption badMode[] = {{5, (OpMode)7, "A", "", ""}};
  const OpModeOption noName[] = {{5, OpMode::AUTO, "", "", ""}};
  const OpModeOption nullName[] = {{5, OpMode::AUTO, nullptr, "", ""}};
  std::string longName(robot::OpModeRegistry::kMaxName + 1, 'x');
  const OpModeOption tooLong[] = {{5, OpMode::AUTO, longName.c_str(), "", ""}};
  EXPECT_TRUE(!reg.set(zeroId, 1));
  EXPECT_TRUE(!reg.set(dupId, 2));
  EXPECT_TRUE(!reg.set(badMode, 1));
  EXPECT_TRUE(!reg.set(noName, 1));
  EXPECT_TRUE(!reg.set(nullName, 1));
  EXPECT_TRUE(!reg.set(tooLong, 1));
  EXPECT_TRUE(!reg.set(nullptr, 3));
  EXPECT_TRUE(!reg.set(good, robot::OpModeRegistry::kMaxOptions + 1));

  OpMode m;
  EXPECT_TRUE(reg.version() == v);
  EXPECT_TRUE(reg.find(1, &m) && m == OpMode::AUTO);

  EXPECT_TRUE(reg.set(nullptr, 0));
  EXPECT_TRUE(!reg.find(1, &m));
  EXPECT_TRUE(readAll(reg, 64) == "{\"options\":[]}");
}

TEST_CASE(opmodes_json_escapes_strings_and_keeps_64_bit_ids_exact){
  robot::OpModeRegistry reg;
  const OpModeOption opts[] = {
    {18446744073709551615ull, OpMode::UTILITY, "Say \"hi\"\\", "G\n1", nullptr},
    {9007199254740993ull, OpMode::TELEOP, "Drive", nullptr, "Arcade"},
  };
  EXPECT_TRUE(reg.set(opts, 2));
  const std::string expected =
    "{\"options\":["
    "{\"id\":\"18446744073709551615\",\"mode\":\"utility\",\"name\":\"Say \\\"hi\\\"\\\\\","
    "\"group\":\"G\\u000a1\",\"description\":\"\"},"
    "{\"id\":\"9007199254740993\",\"mode\":\"teleop\",\"name\":\"Drive\",\"group\":\"\","
    "\"description\":\"Arcade\"}]}";
  EXPECT_TRUE(readAll(reg, 64) == expected);
  EXPECT_TRUE(readAll(reg, 7) == expected);  // chunked reads give the same bytes
}

TEST_CASE(opmodes_read_before_the_first_set_is_the_empty_list){
  robot::OpModeRegistry reg;
  EXPECT_TRUE(reg.version() == 0);
  EXPECT_TRUE(readAll(reg, 5) == "{\"options\":[]}");
}

TEST_CASE(opmodes_read_reports_a_list_replaced_mid_response){
  robot::OpModeRegistry reg;
  const OpModeOption a[] = {{1, OpMode::AUTO, "A", "", ""}};
  EXPECT_TRUE(reg.set(a, 1));
  const uint32_t ver = reg.version();
  char buf[8];
  bool stale = true;
  EXPECT_TRUE(reg.readJson(ver, 0, buf, sizeof buf, &stale) == sizeof buf && !stale);
  EXPECT_TRUE(reg.set(a, 1));
  EXPECT_TRUE(reg.readJson(ver, 8, buf, sizeof buf, &stale) == 0 && stale);
}

TEST_CASE(opmodes_preferred_uses_the_last_pick_while_it_is_listed){
  robot::OpModeRegistry reg;
  const OpModeOption opts[] = {
    {1, OpMode::AUTO, "A1", "", ""}, {2, OpMode::AUTO, "A2", "", ""}, {3, OpMode::TELEOP, "T", "", ""},
  };
  EXPECT_TRUE(reg.set(opts, 3));
  EXPECT_TRUE(reg.preferred(OpMode::AUTO) == 1);
  EXPECT_TRUE(reg.preferred(OpMode::UTILITY) == 0);
  reg.remember(OpMode::AUTO, 2);
  EXPECT_TRUE(reg.preferred(OpMode::AUTO) == 2);
  EXPECT_TRUE(reg.set(opts, 1));             // option 2 is gone
  EXPECT_TRUE(reg.preferred(OpMode::AUTO) == 1);
  EXPECT_TRUE(!reg.hasMode(OpMode::TELEOP));
}

TEST_CASE(selection_changes_only_while_idle){
  robot::StateService rs;
  EXPECT_TRUE(rs.setSelectionIfIdle(1u, OpMode::AUTO, 7));
  EXPECT_TRUE(rs.read().selectedMode == OpMode::AUTO && rs.read().opModeId == 7);

  rs.setStatus(2u, robot::Status::INIT);     // accepted INIT, phase not yet published
  EXPECT_TRUE(!rs.setSelectionIfIdle(3u, OpMode::TELEOP, 8));
  rs.setPhase(3u, robot::Phase::AUTO_INIT);
  rs.setStatus(3u, robot::Status::STOP);
  EXPECT_TRUE(!rs.setSelectionIfIdle(4u, OpMode::TELEOP, 8));
  rs.setPhase(4u, robot::Phase::TRANSITION);
  EXPECT_TRUE(rs.setSelectionIfIdle(5u, OpMode::TELEOP, 8));

  // A Driver Station pick made in between wins over a library correction.
  EXPECT_TRUE(!rs.replaceSelectionIfIdle(6u, OpMode::AUTO, 7, OpMode::AUTO, 9));
  EXPECT_TRUE(rs.replaceSelectionIfIdle(6u, OpMode::TELEOP, 8, OpMode::TELEOP, 9));
  EXPECT_TRUE(rs.read().opModeId == 9);
}

TEST_CASE(ds_set_opmode_options_moves_a_stale_selection){
  resetAll();
  const OpModeOption first[] = {{1, OpMode::TELEOP, "Drive", "", ""}, {2, OpMode::AUTO, "Left", "", ""}};
  EXPECT_TRUE(probot::ds::setOpModeOptions(first, 2));
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 1);   // none selected: first TELEOP option

  const OpModeOption second[] = {{3, OpMode::TELEOP, "Tank", "", ""}};
  EXPECT_TRUE(probot::ds::setOpModeOptions(second, 1));
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 3);   // 1 is gone

  // While active the selection stays, even if it is no longer listed.
  robot::state().setStatus(1u, robot::Status::START);
  robot::state().setPhase(1u, robot::Phase::TELEOP_RUN);
  EXPECT_TRUE(probot::ds::setOpModeOptions(first, 2));
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 3);

  const OpModeOption bad[] = {{0, OpMode::TELEOP, "x", "", ""}};
  EXPECT_TRUE(!probot::ds::setOpModeOptions(bad, 1));
  resetAll();
}

TEST_CASE(ds_reconcile_fixes_a_selection_dropped_during_a_run){
  resetAll();
  const OpModeOption first[] = {{1, OpMode::TELEOP, "Drive", "", ""}, {2, OpMode::TELEOP, "Tank", "", ""}};
  EXPECT_TRUE(probot::ds::setOpModeOptions(first, 2));
  EXPECT_TRUE(robot::state().setSelectionIfIdle(1u, OpMode::TELEOP, 2));

  robot::state().setStatus(2u, robot::Status::START);
  robot::state().setPhase(2u, robot::Phase::TELEOP_RUN);
  EXPECT_TRUE(probot::ds::setOpModeOptions(first, 1));   // 2 dropped mid-run
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 2);

  robot::state().setStatus(3u, robot::Status::STOP);
  robot::state().setPhase(3u, robot::Phase::STOPPED);
  probot::ds::detail::reconcileSelection();              // what INIT runs first
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 1);
  probot::ds::detail::reconcileSelection();              // listed: no change
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 1);
  resetAll();
}

TEST_CASE(ds_utility_selection_falls_back_to_teleop_when_utility_goes_away){
  resetAll();
  const OpModeOption opts[] = {{4, OpMode::UTILITY, "Home arm", "", ""}, {5, OpMode::TELEOP, "Drive", "", ""}};
  EXPECT_TRUE(probot::ds::setOpModeOptions(opts, 2));
  EXPECT_TRUE(robot::state().setSelectionIfIdle(1u, OpMode::UTILITY, 4));
  EXPECT_TRUE(probot::ds::setOpModeOptions(opts + 1, 1));
  EXPECT_TRUE(probot::ds::selectedMode() == OpMode::TELEOP);
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 5);

  // With utility hooks UTILITY stays selectable, with no option.
  __atomic_store_n(&robot::g_utility_hooks, 1u, __ATOMIC_SEQ_CST);
  EXPECT_TRUE(robot::state().setSelectionIfIdle(2u, OpMode::UTILITY, 4));
  EXPECT_TRUE(probot::ds::setOpModeOptions(opts + 1, 1));
  EXPECT_TRUE(probot::ds::selectedMode() == OpMode::UTILITY);
  EXPECT_TRUE(probot::ds::selectedOpModeId() == 0);
  resetAll();
}

TEST_CASE(ds_utility_available_from_hooks_or_options){
  resetAll();
  EXPECT_TRUE(!probot::ds::utilityAvailable());
  const OpModeOption util[] = {{4, OpMode::UTILITY, "Home arm", "", ""}};
  EXPECT_TRUE(probot::ds::setOpModeOptions(util, 1));
  EXPECT_TRUE(probot::ds::utilityAvailable());
  EXPECT_TRUE(probot::ds::setOpModeOptions(nullptr, 0));
  EXPECT_TRUE(!probot::ds::utilityAvailable());
  __atomic_store_n(&robot::g_utility_hooks, 1u, __ATOMIC_SEQ_CST);
  EXPECT_TRUE(probot::ds::utilityAvailable());
  resetAll();
}
