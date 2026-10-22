#include "test_harness.hpp"

#include <probot/dashboard/dashboard.hpp>

#include <map>
#include <string>
#include <vector>

static_assert(PROBOT_HAS_DASH_PERSIST == 1, "feature macro for sketches and SDKs");

namespace dash = probot::dashboard;
using dash::TopicId;
using dash::ValueType;

namespace {
  // In-memory stand-in for NVS.
  struct FakeFlash {
    std::map<std::string, std::vector<uint8_t>> keys;
    int saves = 0, erases = 0;
    bool failSaves = false, failLoads = false;
    void (*onSave)() = nullptr;
  };
  FakeFlash g_flash;

  long fakeLoad(const char* key, uint8_t* buf, size_t cap) {
    if (g_flash.failLoads) return -1;
    auto it = g_flash.keys.find(key);
    if (it == g_flash.keys.end() || it->second.size() > cap) return 0;
    memcpy(buf, it->second.data(), it->second.size());
    return (long)it->second.size();
  }
  bool fakeSave(const char* key, const uint8_t* data, size_t len) {
    ++g_flash.saves;
    if (g_flash.failSaves) return false;
    g_flash.keys[key] = std::vector<uint8_t>(data, data + len);
    if (g_flash.onSave) g_flash.onSave();
    return true;
  }
  bool fakeErase(const char* key) { ++g_flash.erases; g_flash.keys.erase(key); return true; }
  bool fakeEraseAll() { ++g_flash.erases; g_flash.keys.clear(); return true; }
  const dash::detail::PersistBackend kFake{ fakeLoad, fakeSave, fakeErase, fakeEraseAll };

  void fresh(uint32_t maxValue = 1024, uint32_t queue = 2048) {
    dash::detail::resetForTest();
    dash::detail::configure(dash::Limits{ 16, 4, maxValue, 32768, queue, 4096 });
    g_flash = FakeFlash{};
    dash::detail::g_persist = &kFake;
  }

  std::string keyOf(const char* path) {
    char k[dash::detail::kKeyLen];
    dash::detail::persistKey(dash::detail::hashOf(path), k);
    return k;
  }

  void store(const char* path, ValueType vt, const char* type, const void* v, size_t len) {
    uint8_t rec[600];
    size_t n = dash::detail::encodeRecord(path, (uint8_t)vt, type, (const uint8_t*)v, len, rec, sizeof rec);
    g_flash.keys[keyOf(path)] = std::vector<uint8_t>(rec, rec + n);
  }

  std::vector<uint8_t> rec(8192), cur(8192);
  using Why = dash::detail::PersistFailure;
  std::vector<std::pair<std::string, Why>> g_failed;
  void onFailed(const char* path, Why why) { g_failed.emplace_back(path, why); }
  size_t tick(uint32_t now, bool idle = true) {
    size_t cap = dash::detail::recordCapacity(dash::detail::maxValue());
    return dash::detail::persistTick(now, idle, rec.data(), cur.data(), cap, onFailed);
  }
  void write(TopicId id, double v) {
    dash::detail::pushWrite(id, (uint8_t)ValueType::Double, (const uint8_t*)&v, 8);
  }
  double storedValue(const char* path) {
    auto it = g_flash.keys.find(keyOf(path));
    dash::detail::Record r{};
    double d = -1;
    if (it != g_flash.keys.end() && dash::detail::decodeRecord(it->second.data(), it->second.size(), r) && r.len == 8)
      memcpy(&d, r.value, 8);
    return d;
  }

  std::vector<std::pair<TopicId, double>> g_drained;
  void collect(const dash::Write& w, void*) {
    double d = 0;
    if (w.len == 8) memcpy(&d, w.data, 8);
    g_drained.emplace_back(w.id, d);
  }
  void rejectAll(const dash::Write& w, void*) { dash::rejectWrite(w.id, 4); }
  void acceptAll(const dash::Write&, void*) {}

  // A Driver Station edit that robot code accepts.
  void tune(TopicId id, double v) {
    write(id, v);
    dash::drainWrites(acceptAll, nullptr);
  }

  constexpr uint8_t kTunable = dash::TUNABLE | dash::MUTABLE | dash::PERSISTENT;
}

TEST_CASE(dashboard_persist_records_round_trip_and_reject_garbage) {
  uint8_t buf[64];
  const uint8_t v[3] = { 1, 2, 3 };
  size_t n = dash::detail::encodeRecord("/a", 6, "raw", v, 3, buf, sizeof buf);
  EXPECT_TRUE(n == 4 + 2 + 3 + 2 + 3);
  dash::detail::Record r{};
  EXPECT_TRUE(dash::detail::decodeRecord(buf, n, r));
  EXPECT_TRUE(r.vt == 6 && r.nameLen == 2 && memcmp(r.name, "/a", 2) == 0);
  EXPECT_TRUE(r.typeLen == 3 && memcmp(r.type, "raw", 3) == 0 && r.len == 3 && r.value[2] == 3);
  EXPECT_TRUE(!dash::detail::decodeRecord(buf, n - 1, r));        // truncated
  EXPECT_TRUE(!dash::detail::decodeRecord(buf, 5, r));
  buf[0] = 2;
  EXPECT_TRUE(!dash::detail::decodeRecord(buf, n, r));            // unknown version
  EXPECT_TRUE(dash::detail::encodeRecord("/a", 6, "raw", v, 3, buf, n - 1) == 0);

  char k[dash::detail::kKeyLen];
  dash::detail::persistKey(0x0123ABCDu, k);
  EXPECT_TRUE(std::string(k) == "d0123abcd");
}

TEST_CASE(dashboard_persistent_value_is_in_the_first_drain_after_publish) {
  fresh();
  double stored = 0.25;
  store("/Tunables/kP", ValueType::Double, "double", &stored, 8);
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  double def = 1.0;
  dash::set(kp, &def, 8);                                          // default before the first drain
  g_drained.clear();
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 1);
  EXPECT_TRUE(g_drained.size() == 1 && g_drained[0].first == kp && g_drained[0].second == 0.25);

  // Without PERSISTENT, or with another type or a malformed value, nothing is restored.
  store("/Tunables/kI", ValueType::Double, "double", &stored, 8);
  dash::publish("/Tunables/kI", ValueType::Double, "double", nullptr, dash::TUNABLE | dash::MUTABLE);
  store("/Tunables/kD", ValueType::Double, "double", &stored, 8);
  dash::publish("/Tunables/kD", ValueType::Double, "float64", nullptr, kTunable);
  store("/Tunables/kF", ValueType::Double, "double", &stored, 4);
  dash::publish("/Tunables/kF", ValueType::Double, "double", nullptr, kTunable);
  dash::publish("/Tunables/none", ValueType::Double, "double", nullptr, kTunable);
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 0);

  // A restore does not run again for an already published path.
  EXPECT_TRUE(dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable) == kp);
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 0);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_only_accepted_driver_station_values_are_stored) {
  fresh();
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  double a = 1.0, b = 5.0;
  dash::set(kp, &a, 8);                                            // the code default
  tick(0);
  EXPECT_TRUE(tick(60000) == 0 && g_flash.keys.empty());           // never stored
  tune(kp, 2.0);
  EXPECT_TRUE(tick(61000) == 0);                                   // drained write observed
  EXPECT_TRUE(tick(62999) == 0);                                   // still open to refusal
  EXPECT_TRUE(tick(63000, false) == 0);                            // robot running
  EXPECT_TRUE(tick(63000) == 1 && storedValue("/Tunables/kP") == 2.0);
  EXPECT_TRUE(tick(90000) == 0);                                   // stored already

  dash::set(kp, &b, 8);                                            // the robot's own change
  tick(91000);
  EXPECT_TRUE(tick(99000) == 0 && storedValue("/Tunables/kP") == 2.0);
  tune(kp, 2.0);                                                   // same record: not rewritten
  tick(100000);
  EXPECT_TRUE(tick(103000) == 0 && g_flash.saves == 1);
  tune(kp, 4.0);                                                   // tuned while running
  tick(104000, false);
  tick(110000, false);
  EXPECT_TRUE(tick(111000) == 1 && storedValue("/Tunables/kP") == 4.0);

  // The stored record restores after a "reboot", whatever the new default.
  auto flash = g_flash.keys;
  fresh();
  g_flash.keys = flash;
  kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  dash::set(kp, &b, 8);
  g_drained.clear();
  dash::drainWrites(collect, nullptr);
  EXPECT_TRUE(g_drained.size() == 1 && g_drained[0].second == 4.0);
  tick(0);
  EXPECT_TRUE(tick(10000) == 0 && g_flash.saves == 0);             // the restore is not stored again
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_refused_restore_is_erased_without_a_k_frame) {
  fresh();
  double stored = 0.25, def = 1.0, edit = 3.0;
  store("/Tunables/kP", ValueType::Double, "double", &stored, 8);
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  dash::set(kp, &def, 8);
  EXPECT_TRUE(dash::drainWrites(rejectAll, nullptr) == 1);
  uint8_t frame[256];
  while (dash::detail::buildFrame(frame, sizeof frame, 0)) EXPECT_TRUE(frame[0] != 'K');
  EXPECT_TRUE(tick(0, false) == 0 && g_flash.keys.size() == 1);    // erased only while idle
  EXPECT_TRUE(tick(0) == 1 && g_flash.erases == 1 && g_flash.keys.empty());
  EXPECT_TRUE(tick(10000) == 0);                                   // the default is not stored

  // A Driver Station write is refused with a 'K' frame as before.
  dash::detail::pushWrite(kp, (uint8_t)ValueType::Double, (const uint8_t*)&edit, 8);
  EXPECT_TRUE(dash::drainWrites(rejectAll, nullptr) == 1);
  bool sawAck = false;
  while (size_t n = dash::detail::buildFrame(frame, sizeof frame, 0)) sawAck |= frame[0] == 'K' && n == 4;
  EXPECT_TRUE(sawAck);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_driver_station_write_replaces_a_pending_restore) {
  fresh();
  double stored = 0.25, edit = 3.0;
  store("/Tunables/kP", ValueType::Double, "double", &stored, 8);
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  dash::detail::pushWrite(kp, (uint8_t)ValueType::Double, (const uint8_t*)&edit, 8);
  g_drained.clear();
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 1 && g_drained[0].second == 3.0);
  dash::rejectWrite(kp, 4);                                        // a DS write: not stored, no erase
  tick(0);
  EXPECT_TRUE(tick(10000) == 0 && storedValue("/Tunables/kP") == 0.25);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_refused_driver_station_write_is_not_stored_but_the_one_before_it_is) {
  fresh();
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  tune(kp, 2.0);
  write(kp, 3.0);
  dash::drainWrites(rejectAll, nullptr);                           // refused inside the drain
  write(kp, 4.0);
  dash::drainWrites(acceptAll, nullptr);
  dash::rejectWrite(kp, 4);                                        // refused right after it
  EXPECT_TRUE(tick(0) == 1 && storedValue("/Tunables/kP") == 2.0);  // accepted already: no wait
  EXPECT_TRUE(tick(5000) == 0 && g_flash.saves == 1);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_clear_persisted_forgets_until_the_next_driver_station_write) {
  fresh();
  EXPECT_TRUE(dash::clearPersisted());
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  tune(kp, 1.0);
  tick(0);
  tick(3000);
  EXPECT_TRUE(g_flash.keys.size() == 1);
  EXPECT_TRUE(dash::clearPersisted() && g_flash.keys.empty());
  EXPECT_TRUE(tick(10000) == 0 && g_flash.keys.empty());
  tune(kp, 2.0);                                                   // not stored yet when cleared
  EXPECT_TRUE(dash::clearPersisted());
  tick(11000);
  EXPECT_TRUE(tick(14000) == 0 && g_flash.keys.empty());
  tune(kp, 3.0);
  tick(15000);
  EXPECT_TRUE(tick(18000) == 1 && storedValue("/Tunables/kP") == 3.0);

  dash::detail::g_persist = nullptr;
  EXPECT_TRUE(!dash::clearPersisted());
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_failed_store_is_reported_once_per_driver_station_write) {
  fresh();
  g_failed.clear();
  g_flash.failSaves = true;
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  tune(kp, 1.0);
  tick(0);
  EXPECT_TRUE(tick(3000) == 1);
  EXPECT_TRUE(g_failed.size() == 1 && g_failed[0].first == "/Tunables/kP" && g_failed[0].second == Why::Store);
  EXPECT_TRUE(tick(9000) == 0 && g_failed.size() == 1);            // not retried
  g_flash.failSaves = false;
  tune(kp, 2.0);
  tick(10000);
  EXPECT_TRUE(tick(13000) == 1 && storedValue("/Tunables/kP") == 2.0 && g_failed.size() == 1);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_tune_after_a_refused_restore_is_stored) {
  fresh();
  double stored = 0.25, def = 1.0;
  store("/Tunables/kP", ValueType::Double, "double", &stored, 8);
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  dash::set(kp, &def, 8);
  dash::drainWrites(rejectAll, nullptr);
  tune(kp, 3.0);                                                   // tuned while running
  tick(1000, false);
  tick(5000);
  EXPECT_TRUE(storedValue("/Tunables/kP") == 3.0 && g_flash.erases == 0);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_nothing_is_stored_while_a_write_waits_for_the_drain) {
  fresh();
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  tune(kp, 2.0);
  write(kp, 3.0);                                                  // not drained yet
  tick(0);
  EXPECT_TRUE(tick(60000) == 0 && g_flash.keys.empty());
  dash::drainWrites(acceptAll, nullptr);
  tick(61000);
  EXPECT_TRUE(tick(64000) == 1 && storedValue("/Tunables/kP") == 3.0 && g_flash.saves == 1);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_unreadable_store_is_reported_once_and_kept) {
  fresh();
  g_failed.clear();
  double stored = 0.25, def = 1.0;
  store("/Tunables/kP", ValueType::Double, "double", &stored, 8);
  g_flash.failLoads = true;
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  g_flash.failLoads = false;
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 0);
  dash::set(kp, &def, 8);
  tick(0);
  EXPECT_TRUE(tick(60000) == 0 && storedValue("/Tunables/kP") == 0.25);
  EXPECT_TRUE(g_failed.size() == 1 && g_failed[0].second == Why::Read);   // reported once
  tune(kp, 3.0);
  tick(61000);
  EXPECT_TRUE(tick(64000) == 1 && storedValue("/Tunables/kP") == 3.0);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_restores_do_not_use_the_write_queue) {
  fresh(1024, 8);                                                  // room for one 8-byte write
  double stored = 0.25;
  store("/Tunables/a", ValueType::Double, "double", &stored, 8);
  store("/Tunables/b", ValueType::Double, "double", &stored, 8);
  TopicId a = dash::publish("/Tunables/a", ValueType::Double, "double", nullptr, kTunable);
  dash::publish("/Tunables/b", ValueType::Double, "double", nullptr, kTunable);
  TopicId c = dash::publish("/Tunables/c", ValueType::Double, "double", nullptr, kTunable);
  double v = 2.0;
  EXPECT_TRUE(dash::detail::pushWrite(c, 4, (const uint8_t*)&v, 8) == dash::WriteStatus::Ok);
  EXPECT_TRUE(dash::detail::pushWrite(a, 4, (const uint8_t*)&v, 8) == dash::WriteStatus::QueueFull);
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 3);
  EXPECT_TRUE(dash::detail::pushWrite(a, 4, (const uint8_t*)&v, 8) == dash::WriteStatus::Ok);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_clear_persisted_drops_queued_restores_and_races_with_a_save) {
  fresh();
  double stored = 0.25;
  store("/Tunables/kP", ValueType::Double, "double", &stored, 8);
  dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  EXPECT_TRUE(dash::clearPersisted() && g_flash.keys.empty());
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 0);

  // A clear that runs while a value is being saved still wins.
  TopicId ki = dash::publish("/Tunables/kI", ValueType::Double, "double", nullptr, kTunable);
  tune(ki, 1.0);
  tick(0);
  g_flash.onSave = [] { g_flash.onSave = nullptr; dash::clearPersisted(); };
  tick(3000);
  EXPECT_TRUE(g_flash.keys.empty());
  EXPECT_TRUE(tick(9000) == 0);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_values_over_the_persist_limit_are_reported) {
  fresh(4096, 8192);
  g_failed.clear();
  TopicId big = dash::publish("/Tunables/blob", ValueType::Raw, "raw", nullptr, kTunable);
  std::vector<uint8_t> v(dash::detail::kMaxPersistValue + 1, 7);
  dash::detail::pushWrite(big, (uint8_t)ValueType::Raw, v.data(), v.size());
  dash::drainWrites(acceptAll, nullptr);
  tick(0);
  tick(3000);
  EXPECT_TRUE(g_flash.keys.empty() && g_failed.size() == 1 && g_failed[0].second == Why::Store);
  dash::detail::pushWrite(big, (uint8_t)ValueType::Raw, v.data(), v.size() - 1);
  dash::drainWrites(acceptAll, nullptr);
  tick(4000);
  EXPECT_TRUE(tick(7000) == 1 && g_flash.keys.size() == 1);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_unpublish_frees_values_not_stored_yet) {
  fresh();
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  tune(kp, 1.0);
  tune(kp, 2.0);                                                   // one accepted, one candidate
  dash::unpublish(kp);
  tick(0);
  EXPECT_TRUE(tick(5000) == 0 && g_flash.keys.empty());
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_oversized_values_are_reported_without_being_kept) {
  fresh(4096, 8192);
  g_failed.clear();
  TopicId big = dash::publish("/Tunables/blob", ValueType::Raw, "raw", nullptr, kTunable);
  std::vector<uint8_t> v(dash::detail::kMaxPersistValue + 1, 7);
  dash::detail::pushWrite(big, (uint8_t)ValueType::Raw, v.data(), v.size());
  dash::drainWrites(acceptAll, nullptr);
  EXPECT_TRUE(!dash::detail::liveSlot(dash::detail::g_store, big)->cand);
  tick(0, false);                                                  // reported while running too
  EXPECT_TRUE(g_failed.size() == 1 && g_failed[0].second == Why::Store);
  // Refused after the drain: nothing to report.
  dash::detail::pushWrite(big, (uint8_t)ValueType::Raw, v.data(), v.size());
  dash::drainWrites(acceptAll, nullptr);
  dash::rejectWrite(big, 1);
  tick(1000);
  EXPECT_TRUE(g_failed.size() == 1 && g_flash.keys.empty());
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_clearing_the_persistent_flag_drops_values_not_stored_yet) {
  fresh();
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  tune(kp, 1.0);
  tune(kp, 2.0);
  EXPECT_TRUE(dash::setFlags(kp, dash::TUNABLE | dash::MUTABLE));
  EXPECT_TRUE(dash::setFlags(kp, kTunable));
  tick(0);
  EXPECT_TRUE(tick(5000) == 0 && g_flash.keys.empty());
  tune(kp, 3.0);                                                   // stored again after a new write
  tick(6000);
  EXPECT_TRUE(tick(9000) == 1 && storedValue("/Tunables/kP") == 3.0);
  dash::detail::resetForTest();
}

namespace {
  TopicId g_clearTopic;
  size_t g_ticksDuringClear;
  bool eraseAllWhileTuning() {
    tune(g_clearTopic, 4.0);                                       // accepted while the walk ran
    tick(0);
    g_ticksDuringClear = tick(5000);                               // not stored before the erase
    return fakeEraseAll();
  }
  const dash::detail::PersistBackend kClearRace{ fakeLoad, fakeSave, fakeErase, eraseAllWhileTuning };
}

TEST_CASE(dashboard_values_accepted_during_clear_persisted_are_stored_after_it) {
  fresh();
  dash::detail::g_persist = &kClearRace;
  g_clearTopic = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  EXPECT_TRUE(dash::clearPersisted());
  EXPECT_TRUE(g_ticksDuringClear == 0 && g_flash.keys.empty());
  EXPECT_TRUE(tick(6000) == 1 && storedValue("/Tunables/kP") == 4.0);
  dash::detail::resetForTest();
}

namespace {
  void clearThenRefuse(const dash::Write& w, void*) {
    dash::clearPersisted();
    dash::rejectWrite(w.id, 1);
  }
}

TEST_CASE(dashboard_restore_refused_after_clear_persisted_sends_no_k_frame) {
  fresh();
  double stored = 0.25;
  store("/Tunables/kP", ValueType::Double, "double", &stored, 8);
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, kTunable);
  EXPECT_TRUE(dash::drainWrites(clearThenRefuse, nullptr) == 1);
  uint8_t frame[256];
  while (dash::detail::buildFrame(frame, sizeof frame, 0)) EXPECT_TRUE(frame[0] != 'K');
  EXPECT_TRUE(g_flash.keys.empty());
  (void)kp;
  dash::detail::resetForTest();
}
