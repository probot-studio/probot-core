#include "test_harness.hpp"

#include <probot/core/lifecycle.hpp>
#include <probot/robot/state.hpp>

#include <string>
#include <vector>

namespace robot = probot::robot;
namespace core  = probot::core;
using robot::OpMode;
using core::Stage;

namespace {
  std::vector<std::string> g_calls;
  int g_neutralized = 0;
  uint32_t g_clock_now = 0;

  void h_autoInit()       { g_calls.push_back("autonomousInit"); }
  void h_autoInitLoop()   { g_calls.push_back("autonomousInitLoop"); }
  void h_autoStart()      { g_calls.push_back("autonomousStart"); }
  void h_autoLoop()       { g_calls.push_back("autonomousLoop"); }
  void h_autoStop()       { g_calls.push_back("autonomousStop"); }
  void h_teleopInit()     { g_calls.push_back("teleopInit"); }
  void h_teleopInitLoop() { g_calls.push_back("teleopInitLoop"); }
  void h_teleopStart()    { g_calls.push_back("teleopStart"); }
  void h_teleopLoop()     { g_calls.push_back("teleopLoop"); }
  void h_teleopStop()     { g_calls.push_back("teleopStop"); }
  void h_neutralize()     { ++g_neutralized; }
  uint32_t h_clockNow()    { return g_clock_now; }

  core::Hooks mkHooks(){
    return core::Hooks{ h_autoInit, h_autoInitLoop, h_autoStart, h_autoLoop,
                        h_autoStop, h_teleopInit, h_teleopInitLoop,
                        h_teleopStart, h_teleopLoop, h_teleopStop };
  }
}

TEST_CASE(teleop_init_loop_start_loop_stop_order){
  g_calls.clear(); g_neutralized = 0;
  robot::StateService rs;
  core::PhaseMachine m;
  auto h = mkHooks();
  volatile uint32_t hb = 0;

  m.step(h, {OpMode::TELEOP, Stage::INIT}, rs, 100, h_neutralize, &hb);
  m.step(h, {OpMode::TELEOP, Stage::INIT}, rs, 120, h_neutralize, &hb);
  EXPECT_TRUE(rs.read().phase == robot::Phase::TELEOP_INIT);

  m.step(h, {OpMode::TELEOP, Stage::RUN}, rs, 140, h_neutralize, &hb);
  m.step(h, {OpMode::TELEOP, Stage::RUN}, rs, 160, h_neutralize, &hb);
  EXPECT_TRUE(rs.read().phase == robot::Phase::TELEOP_RUN);

  m.step(h, {OpMode::TELEOP, Stage::STOPPED}, rs, 180, h_neutralize, &hb);
  EXPECT_TRUE(rs.read().phase == robot::Phase::STOPPED);

  const std::vector<std::string> expected{
    "teleopInit", "teleopInitLoop", "teleopInitLoop",
    "teleopStart", "teleopLoop", "teleopLoop", "teleopStop"
  };
  EXPECT_TRUE(g_calls == expected);
  EXPECT_TRUE(g_neutralized >= 4); // INIT and STOPPED keep input neutral.
}

TEST_CASE(auto_period_stops_then_transitions_to_teleop){
  g_calls.clear();
  robot::StateService rs;
  core::PhaseMachine m;
  core::Supervisor sup;
  auto h = mkHooks();

  rs.setControl(10, robot::Status::STOP, OpMode::AUTO);
  m.step(h, sup.update(rs, 10, false), rs, 10);
  rs.setStatus(20, robot::Status::INIT);
  m.step(h, sup.update(rs, 20, false), rs, 20);
  rs.setStatus(30, robot::Status::START);
  g_clock_now = 1100; // autonomousStart returned after the step's initial timestamp
  m.step(h, sup.update(rs, 30, false), rs, 1000, nullptr, nullptr, h_clockNow);
  EXPECT_TRUE(rs.read().phase == robot::Phase::AUTO_RUN);
  EXPECT_TRUE(rs.read().autoStartMs == 1100u);

  rs.setAutoPeriodSeconds(40, 5);
  auto desired = sup.update(rs, 6100, false);
  EXPECT_TRUE(desired.mode == OpMode::TELEOP);
  EXPECT_TRUE(desired.stage == Stage::TRANSITION);
  m.step(h, desired, rs, 6100);

  auto s = rs.read();
  EXPECT_TRUE(s.phase == robot::Phase::TRANSITION);
  EXPECT_TRUE(s.selectedMode == OpMode::TELEOP);
  EXPECT_TRUE(s.status == robot::Status::STOP);
  EXPECT_TRUE(g_calls.back() == "autonomousStop");
}

TEST_CASE(transition_init_enters_teleop_init){
  g_calls.clear();
  robot::StateService rs;
  core::PhaseMachine m;
  auto h = mkHooks();

  m.step(h, {OpMode::TELEOP, Stage::TRANSITION}, rs, 10);
  m.step(h, {OpMode::TELEOP, Stage::INIT}, rs, 20);
  EXPECT_TRUE(rs.read().phase == robot::Phase::TELEOP_INIT);
  EXPECT_TRUE(g_calls.size() == 2);
  EXPECT_TRUE(g_calls[0] == "teleopInit");
  EXPECT_TRUE(g_calls[1] == "teleopInitLoop");
}

TEST_CASE(stop_from_init_calls_active_stop_once){
  g_calls.clear();
  robot::StateService rs;
  core::PhaseMachine m;
  auto h = mkHooks();

  m.step(h, {OpMode::TELEOP, Stage::INIT}, rs, 10);
  m.step(h, {OpMode::TELEOP, Stage::STOPPED}, rs, 20);
  m.step(h, {OpMode::TELEOP, Stage::STOPPED}, rs, 30);
  EXPECT_TRUE(g_calls.back() == "teleopStop");
  int stops = 0;
  for (const auto& call : g_calls) if (call == "teleopStop") ++stops;
  EXPECT_TRUE(stops == 1);
}

TEST_CASE(mode_change_is_rejected_while_init_or_run){
  EXPECT_TRUE(core::canSelectMode(robot::Phase::STOPPED));
  EXPECT_TRUE(core::canSelectMode(robot::Phase::TRANSITION));
  EXPECT_TRUE(!core::canSelectMode(robot::Phase::AUTO_INIT));
  EXPECT_TRUE(!core::canSelectMode(robot::Phase::AUTO_RUN));
  EXPECT_TRUE(!core::canSelectMode(robot::Phase::TELEOP_INIT));
  EXPECT_TRUE(!core::canSelectMode(robot::Phase::TELEOP_RUN));
}

// HTTP handler'ın kullandığı komut-kabul predicate'leri: public phase, hook
// dönene kadar geride kalır — status alanı bu pencerede duplicate komutu keser.
TEST_CASE(command_acceptance_closes_status_phase_gap){
  using robot::Phase; using robot::Status;
  // INIT: yalnız STOPPED/TRANSITION + status STOP
  EXPECT_TRUE(core::canAcceptInit(Phase::STOPPED, Status::STOP));
  EXPECT_TRUE(core::canAcceptInit(Phase::TRANSITION, Status::STOP));
  EXPECT_TRUE(!core::canAcceptInit(Phase::STOPPED, Status::INIT));   // INIT kabul edildi, faz henüz eski
  EXPECT_TRUE(!core::canAcceptInit(Phase::AUTO_RUN, Status::STOP));
  // START: yalnız INIT fazı + status hâlâ INIT (ikinci START 409)
  EXPECT_TRUE(core::canAcceptStart(Phase::AUTO_INIT, Status::INIT));
  EXPECT_TRUE(core::canAcceptStart(Phase::TELEOP_INIT, Status::INIT));
  EXPECT_TRUE(!core::canAcceptStart(Phase::AUTO_INIT, Status::START)); // start() koşarken duplicate
  EXPECT_TRUE(!core::canAcceptStart(Phase::STOPPED, Status::INIT));
  EXPECT_TRUE(!core::canAcceptStart(Phase::AUTO_RUN, Status::START));
  // MODE: INIT/RUN'da asla
  EXPECT_TRUE(core::canAcceptModeChange(Phase::TRANSITION, Status::STOP));
  EXPECT_TRUE(!core::canAcceptModeChange(Phase::TRANSITION, Status::INIT));
  EXPECT_TRUE(!core::canAcceptModeChange(Phase::TELEOP_RUN, Status::START));
  // STOP: in INIT/RUN, or while an accepted INIT/START is still pending
  EXPECT_TRUE(core::canAcceptStop(Phase::AUTO_INIT, Status::INIT));
  EXPECT_TRUE(core::canAcceptStop(Phase::TELEOP_RUN, Status::START));
  EXPECT_TRUE(core::canAcceptStop(Phase::STOPPED, Status::INIT));      // INIT accepted, phase not yet published
  EXPECT_TRUE(core::canAcceptStop(Phase::TRANSITION, Status::INIT));
  EXPECT_TRUE(!core::canAcceptStop(Phase::STOPPED, Status::STOP));
  EXPECT_TRUE(!core::canAcceptStop(Phase::TRANSITION, Status::STOP));
}

TEST_CASE(stale_auto_start_does_not_expire_before_auto_run){
  robot::StateService rs;
  core::Supervisor sup;
  rs.setControl(0, robot::Status::START, OpMode::AUTO);
  rs.setPhase(0, robot::Phase::AUTO_INIT);
  rs.setAutoStartMs(0, 1000);              // stale stamp from an older run
  rs.setAutoPeriodSeconds(0, 5);

  auto desired = sup.update(rs, 9000, false);
  EXPECT_TRUE(desired.mode == OpMode::AUTO);
  EXPECT_TRUE(desired.stage == Stage::RUN);
  EXPECT_TRUE(rs.read().selectedMode == OpMode::AUTO);
  EXPECT_TRUE(rs.read().status == robot::Status::START);
}

namespace {
  volatile uint32_t* g_hb = nullptr;
  std::vector<std::pair<std::string, uint32_t>> g_hb_seen;
  void rec(const char* name) { g_hb_seen.emplace_back(name, *g_hb); }
  void hb_init()     { rec("init"); }
  void hb_initLoop() { rec("initLoop"); }
  void hb_start()    { rec("start"); }
  void hb_loop()     { rec("loop"); }
  void hb_stop()     { rec("stop"); }
}

// One-shot hooks (init/start/stop) may legitimately block for seconds, e.g.
// gyro calibration in init(). They get PROBOT_HOOK_DEADLINE_MS instead of
// the loop deadline: their heartbeat is stamped into the future by the
// difference, so the supervisor's loop-deadline check fires only after the
// hook budget.
TEST_CASE(stall_heartbeat_gives_one_shot_hooks_the_hook_budget){
  g_hb_seen.clear();
  robot::StateService rs;
  core::PhaseMachine m;
  core::Hooks h{ hb_init, hb_initLoop, hb_start, hb_loop, hb_stop,
                 hb_init, hb_initLoop, hb_start, hb_loop, hb_stop };
  volatile uint32_t hb = 500;
  g_hb = &hb;

  m.step(h, {OpMode::AUTO, Stage::INIT}, rs, 1000, nullptr, &hb);
  m.step(h, {OpMode::AUTO, Stage::RUN}, rs, 2000, nullptr, &hb);
  m.step(h, {OpMode::AUTO, Stage::STOPPED}, rs, 3000, nullptr, &hb);

  const uint32_t grace = PROBOT_HOOK_DEADLINE_MS - PROBOT_LOOP_DEADLINE_MS;
  const std::vector<std::pair<std::string, uint32_t>> expected{
    {"init", 1000 + grace}, {"initLoop", 1000}, {"start", 2000 + grace},
    {"loop", 2000}, {"stop", 3000 + grace}
  };
  EXPECT_TRUE(g_hb_seen == expected);

  // A 3 s init() is fine; one that never returns is flagged after the budget.
  const uint32_t initHb = 1000 + grace;
  EXPECT_TRUE(!core::isStalled(robot::Phase::AUTO_INIT, 1000 + 3000, initHb, PROBOT_LOOP_DEADLINE_MS));
  EXPECT_TRUE(!core::isStalled(robot::Phase::AUTO_INIT, 1000 + PROBOT_HOOK_DEADLINE_MS, initHb, PROBOT_LOOP_DEADLINE_MS));
  EXPECT_TRUE(core::isStalled(robot::Phase::AUTO_INIT, 1000 + PROBOT_HOOK_DEADLINE_MS + 1, initHb, PROBOT_LOOP_DEADLINE_MS));
  g_hb = nullptr;
}

TEST_CASE(stall_detection_includes_init_loop){
  using core::isStalled;
  EXPECT_TRUE(isStalled(robot::Phase::AUTO_INIT, 5000, 1000, 2000));
  EXPECT_TRUE(isStalled(robot::Phase::TELEOP_INIT, 5000, 1000, 2000));
  EXPECT_TRUE(isStalled(robot::Phase::AUTO_RUN, 5000, 1000, 2000));
  EXPECT_TRUE(isStalled(robot::Phase::TELEOP_RUN, 2500, 1000, 2000) == false);
  EXPECT_TRUE(isStalled(robot::Phase::STOPPED, 5000, 1000, 2000) == false);
  EXPECT_TRUE(isStalled(robot::Phase::TRANSITION, 5000, 1000, 2000) == false);
  EXPECT_TRUE(isStalled(robot::Phase::AUTO_INIT, 5000, 0, 2000) == false);
}
