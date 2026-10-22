#include "test_harness.hpp"

#include <probot/dashboard/dashboard.hpp>

#include <string>
#include <vector>

namespace dash = probot::dashboard;
using dash::TopicId;
using dash::ValueType;

namespace {
  dash::Limits limits(uint32_t pool = 32768, uint32_t queue = 2048, uint16_t topics = 16){
    return dash::Limits{ topics, 4, 1024, pool, queue };
  }

  void fresh(dash::Limits lim = limits()){
    dash::detail::resetForTest();
    dash::detail::configure(lim);
  }

  struct Frame { std::vector<uint8_t> b; };

  Frame next(uint32_t now = 0){
    static uint8_t buf[4096];
    size_t n = dash::detail::buildFrame(buf, sizeof(buf), now);
    return Frame{ std::vector<uint8_t>(buf, buf + n) };
  }

  uint16_t u16(const std::vector<uint8_t>& b, size_t at){ return (uint16_t)(b[at] | (b[at + 1] << 8)); }

  // Ids listed in an 'A' frame.
  std::vector<TopicId> announced(const Frame& f){
    std::vector<TopicId> ids;
    size_t p = 1;
    while (p < f.b.size()) {
      ids.push_back(u16(f.b, p));
      p += 4;
      p += 1 + f.b[p];                       // name
      p += 1 + f.b[p];                       // type
      p += 2 + u16(f.b, p);                  // props
    }
    return ids;
  }

  struct Value { TopicId id; std::vector<uint8_t> bytes; };
  std::vector<Value> values(const Frame& f){
    std::vector<Value> out;
    size_t p = 5;
    while (p < f.b.size()) {
      TopicId id = u16(f.b, p);
      uint16_t len = u16(f.b, p + 2);
      out.push_back(Value{ id, std::vector<uint8_t>(f.b.begin() + p + 4, f.b.begin() + p + 4 + len) });
      p += 4 + len;
    }
    return out;
  }

  std::vector<std::pair<TopicId, std::vector<uint8_t>>> g_drained;
  void collect(const dash::Write& w, void*){
    g_drained.emplace_back(w.id, std::vector<uint8_t>(w.data, w.data + w.len));
  }
}

TEST_CASE(dashboard_publish_needs_the_runtime_limits){
  dash::detail::resetForTest();
  EXPECT_TRUE(dash::publish("/Telemetry/x", ValueType::Double, "double", nullptr, dash::TELEMETRY) == dash::kInvalid);
  fresh();
  EXPECT_TRUE(dash::publish("/Telemetry/x", ValueType::Double, "double", nullptr, dash::TELEMETRY) != dash::kInvalid);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_publish_is_idempotent_per_path_and_type){
  fresh();
  TopicId a = dash::publish("/Telemetry/x", ValueType::Double, "double", nullptr, dash::TELEMETRY);
  EXPECT_TRUE(dash::publish("/Telemetry/x", ValueType::Double, "double", nullptr, dash::TELEMETRY) == a);
  EXPECT_TRUE(dash::publish("/Telemetry/x", ValueType::Int, "int", nullptr, dash::TELEMETRY) == dash::kInvalid);
  EXPECT_TRUE(dash::publish("/Telemetry/x", ValueType::Double, "float64", nullptr, dash::TELEMETRY) == dash::kInvalid);
  EXPECT_TRUE(dash::publish("", ValueType::Double, "double", nullptr, 0) == dash::kInvalid);
  std::string longName(256, 'n');
  EXPECT_TRUE(dash::publish(longName.c_str(), ValueType::Double, "double", nullptr, 0) == dash::kInvalid);
  EXPECT_TRUE(dash::stats().topics == 1);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_announces_before_values_and_drops_duplicates){
  fresh();
  TopicId a = dash::publish("/Telemetry/a", ValueType::Int, "int", "{\"unit\":\"m\"}", dash::TELEMETRY);
  int64_t v = 42;
  EXPECT_TRUE(dash::set(a, &v, sizeof(v)));

  Frame f = next();
  EXPECT_TRUE(!f.b.empty() && f.b[0] == 'A');
  EXPECT_TRUE(announced(f) == std::vector<TopicId>{ a });
  EXPECT_TRUE(f.b[3] == (uint8_t)ValueType::Int && f.b[4] == dash::TELEMETRY);

  f = next(1234);
  EXPECT_TRUE(!f.b.empty() && f.b[0] == 'V');
  EXPECT_TRUE(f.b[1] == 0xD2 && f.b[2] == 0x04);          // serverMs little-endian
  auto vals = values(f);
  EXPECT_TRUE(vals.size() == 1 && vals[0].id == a && vals[0].bytes.size() == 8 && vals[0].bytes[0] == 42);
  EXPECT_TRUE(next().b.empty());

  EXPECT_TRUE(dash::set(a, &v, sizeof(v)));                 // identical: dropped
  EXPECT_TRUE(next().b.empty());
  dash::setFlags(a, dash::TELEMETRY | dash::KEEP_DUPLICATES);
  EXPECT_TRUE(next().b[0] == 'A');                          // flags re-announce
  EXPECT_TRUE(next().b[0] == 'V');
  EXPECT_TRUE(dash::set(a, &v, sizeof(v)));
  EXPECT_TRUE(next().b[0] == 'V');                          // kept duplicate
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_unpublish_sends_remove_before_reusing_the_slot){
  fresh(limits(32768, 2048, 1));
  TopicId a = dash::publish("/Telemetry/a", ValueType::Boolean, "boolean", nullptr, dash::TELEMETRY);
  next();
  dash::unpublish(a);
  EXPECT_TRUE(dash::stats().topics == 0);
  // The only slot waits for the 'R' frame, so the table is still full.
  EXPECT_TRUE(dash::publish("/Telemetry/b", ValueType::Boolean, "boolean", nullptr, 0) == dash::kInvalid);
  Frame f = next();
  EXPECT_TRUE(f.b.size() == 3 && f.b[0] == 'R' && u16(f.b, 1) == a);
  TopicId b = dash::publish("/Telemetry/b", ValueType::Boolean, "boolean", nullptr, 0);
  EXPECT_TRUE(b != dash::kInvalid && b != a);               // same slot, new generation
  uint8_t one = 1;
  EXPECT_TRUE(!dash::set(a, &one, 1));                      // stale id is refused
  EXPECT_TRUE(dash::set(b, &one, 1));
  EXPECT_TRUE(dash::stats().bytesUsed > 0);
  dash::unpublish(b);
  EXPECT_TRUE(dash::stats().bytesUsed == 0);                // accounting returns to zero
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_writes_are_validated_and_last_writer_wins){
  using dash::WriteStatus;
  fresh();
  TopicId ro  = dash::publish("/Telemetry/ro", ValueType::Double, "double", nullptr, dash::TELEMETRY);
  TopicId kp  = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, dash::TUNABLE | dash::MUTABLE);
  TopicId ki  = dash::publish("/Tunables/kI", ValueType::Double, "double", nullptr, dash::TUNABLE | dash::MUTABLE);
  double one = 1.0, two = 2.0, three = 3.0;
  dash::set(kp, &one, 8);
  dash::set(ki, &one, 8);
  while (!next().b.empty()) {}

  const uint8_t* p1 = reinterpret_cast<const uint8_t*>(&one);
  const uint8_t* p2 = reinterpret_cast<const uint8_t*>(&two);
  const uint8_t* p3 = reinterpret_cast<const uint8_t*>(&three);
  EXPECT_TRUE(dash::detail::pushWrite(0x00FE, 4, p1, 8) == WriteStatus::UnknownId);
  EXPECT_TRUE(dash::detail::pushWrite(ro, 4, p1, 8) == WriteStatus::Immutable);
  EXPECT_TRUE(dash::detail::pushWrite(kp, 3, p1, 4) == WriteStatus::TypeMismatch);
  std::vector<uint8_t> big(1025, 0);
  EXPECT_TRUE(dash::detail::pushWrite(kp, 4, big.data(), big.size()) == WriteStatus::DecodeError);

  Frame k = next();
  EXPECT_TRUE(k.b.size() == 1 + 4 * 3 && k.b[0] == 'K');
  EXPECT_TRUE(u16(k.b, 4) == ro && k.b[6] == (uint8_t)WriteStatus::Immutable);

  EXPECT_TRUE(dash::detail::pushWrite(kp, 4, p2, 8) == WriteStatus::Ok);
  EXPECT_TRUE(dash::detail::pushWrite(ki, 4, p2, 8) == WriteStatus::Ok);
  EXPECT_TRUE(dash::detail::pushWrite(kp, 4, p3, 8) == WriteStatus::Ok);   // replaces kp's pending write

  g_drained.clear();
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 2);
  EXPECT_TRUE(g_drained.size() == 2 && g_drained[0].first == kp && g_drained[1].first == ki);
  EXPECT_TRUE(g_drained[0].second == std::vector<uint8_t>(p3, p3 + 8));
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 0);

  // Each drained topic re-sends its value so the page leaves "pending".
  auto vals = values(next());
  EXPECT_TRUE(vals.size() == 2);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_write_queue_and_pool_limits_fail_cleanly){
  using dash::WriteStatus;
  fresh(limits(48, 10));
  TopicId kp = dash::publish("/Tunables/kP", ValueType::Double, "double", nullptr, dash::TUNABLE | dash::MUTABLE);
  EXPECT_TRUE(kp != dash::kInvalid);
  // 48-byte pool: these strings fit, a long second name does not.
  EXPECT_TRUE(dash::publish("/Tunables/a-much-longer-topic-name", ValueType::Double, "double", nullptr, 0) == dash::kInvalid);
  double v = 1.0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  EXPECT_TRUE(dash::detail::pushWrite(kp, 4, p, 8) == WriteStatus::Ok);
  dash::TopicId other = dash::publish("/T/k", ValueType::Double, "double", nullptr, dash::TUNABLE | dash::MUTABLE);
  EXPECT_TRUE(other != dash::kInvalid);
  EXPECT_TRUE(dash::detail::pushWrite(other, 4, p, 8) == WriteStatus::QueueFull);
  EXPECT_TRUE(dash::stats().dropped == 1);

  // A value that would exceed the pool is refused, not truncated.
  std::vector<uint8_t> big(64, 1);
  EXPECT_TRUE(!dash::set(kp, big.data(), big.size()));
  dash::unpublish(kp);
  dash::unpublish(other);
  EXPECT_TRUE(dash::stats().bytesUsed == 0);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_resync_reannounces_and_resends_schemas_and_values){
  fresh();
  const char* schema = "Translation2d translation;Rotation2d rotation";
  EXPECT_TRUE(dash::addSchema("struct:Pose2d", "structschema", schema, strlen(schema)));
  EXPECT_TRUE(dash::addSchema("struct:Pose2d", "structschema", schema, strlen(schema)));
  EXPECT_TRUE(dash::hasSchema("struct:Pose2d"));
  EXPECT_TRUE(!dash::hasSchema("struct:Twist2d"));
  TopicId pose = dash::publish("/Telemetry/pose", ValueType::Raw, "struct:Pose2d", nullptr, dash::TELEMETRY);
  uint8_t raw[24] = { 7 };
  dash::set(pose, raw, sizeof(raw));

  std::vector<char> kinds;
  for (Frame f = next(); !f.b.empty(); f = next()) kinds.push_back((char)f.b[0]);
  EXPECT_TRUE((kinds == std::vector<char>{ 'C', 'A', 'V' }));

  dash::detail::requestResync();
  kinds.clear();
  for (Frame f = next(); !f.b.empty(); f = next()) kinds.push_back((char)f.b[0]);
  EXPECT_TRUE((kinds == std::vector<char>{ 'C', 'A', 'V' }));
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_value_batches_split_near_the_frame_target){
  fresh(limits(65536, 2048, 8));
  std::vector<uint8_t> v(600, 9);
  for (int i = 0; i < 4; ++i) {
    std::string name = "/Telemetry/v" + std::to_string(i);
    TopicId id = dash::publish(name.c_str(), ValueType::Raw, "raw", nullptr, dash::TELEMETRY);
    dash::set(id, v.data(), v.size());
  }
  EXPECT_TRUE(next().b[0] == 'A');
  size_t records = 0, frames = 0;
  for (Frame f = next(); !f.b.empty(); f = next()) {
    EXPECT_TRUE(f.b[0] == 'V');
    EXPECT_TRUE(f.b.size() <= dash::detail::kFrameTarget);
    records += values(f).size();
    ++frames;
  }
  EXPECT_TRUE(records == 4 && frames == 2);
  dash::detail::resetForTest();
}

// With more dirty bytes than one push tick sends, every topic still gets
// its turn (the 'V' scan resumes where the previous batch stopped).
TEST_CASE(dashboard_value_scan_is_round_robin_under_a_byte_budget){
  fresh(limits(65536, 2048, 16));
  std::vector<TopicId> ids;
  std::vector<uint8_t> v(500, 1);
  for (int i = 0; i < 16; ++i) {
    std::string name = "/Telemetry/r" + std::to_string(i);
    ids.push_back(dash::publish(name.c_str(), ValueType::Raw, "raw", nullptr, dash::TELEMETRY | dash::KEEP_DUPLICATES));
  }
  while (!next().b.empty()) {}
  std::vector<int> seen(16, 0);
  for (int tick = 0; tick < 16; ++tick) {
    for (TopicId id : ids) dash::set(id, v.data(), v.size());   // all dirty every tick
    Frame f = next();                                          // budget: one frame per tick
    for (auto& val : values(f)) seen[val.id & 0xFF]++;
  }
  for (int i = 0; i < 16; ++i) EXPECT_TRUE(seen[i] > 0);
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_rejects_writes_whose_length_does_not_fit_the_type){
  using dash::WriteStatus;
  fresh();
  const uint8_t f = dash::TUNABLE | dash::MUTABLE;
  TopicId b  = dash::publish("/Tunables/b", ValueType::Boolean, "boolean", nullptr, f);
  TopicId d  = dash::publish("/Tunables/d", ValueType::Double, "double", nullptr, f);
  TopicId fa = dash::publish("/Tunables/fa", ValueType::FloatArray, "float[]", nullptr, f);
  TopicId sa = dash::publish("/Tunables/sa", ValueType::StringArray, "string[]", nullptr, f);
  TopicId s  = dash::publish("/Tunables/s", ValueType::String, "string", nullptr, f);
  uint8_t bytes[16] = { 1, 0, 'x', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  EXPECT_TRUE(dash::detail::pushWrite(b, 1, bytes, 2) == WriteStatus::DecodeError);
  EXPECT_TRUE(dash::detail::pushWrite(b, 1, bytes, 1) == WriteStatus::Ok);
  EXPECT_TRUE(dash::detail::pushWrite(d, 4, bytes, 3) == WriteStatus::DecodeError);
  EXPECT_TRUE(dash::detail::pushWrite(d, 4, bytes, 8) == WriteStatus::Ok);
  EXPECT_TRUE(dash::detail::pushWrite(fa, 9, bytes, 6) == WriteStatus::DecodeError);
  EXPECT_TRUE(dash::detail::pushWrite(fa, 9, bytes, 8) == WriteStatus::Ok);
  EXPECT_TRUE(dash::detail::pushWrite(sa, 11, bytes, 2) == WriteStatus::DecodeError);   // len 1, no byte
  EXPECT_TRUE(dash::detail::pushWrite(sa, 11, bytes, 3) == WriteStatus::Ok);            // ["x"]
  EXPECT_TRUE(dash::detail::pushWrite(s, 5, bytes, 0) == WriteStatus::Ok);              // empty string
  dash::detail::resetForTest();
}

TEST_CASE(dashboard_handles_empty_values){
  fresh();
  TopicId s = dash::publish("/Telemetry/s", ValueType::String, "string", nullptr, dash::TELEMETRY);
  EXPECT_TRUE(dash::set(s, nullptr, 0));
  EXPECT_TRUE(next().b[0] == 'A');
  auto vals = values(next());
  EXPECT_TRUE(vals.size() == 1 && vals[0].bytes.empty());
  EXPECT_TRUE(dash::set(s, nullptr, 0));                    // identical empty value: dropped
  EXPECT_TRUE(next().b.empty());
  dash::detail::resetForTest();
}

// Unpublishing a topic with a pending write removes it from the write FIFO,
// also when the FIFO has wrapped around.
TEST_CASE(dashboard_unpublish_drops_pending_writes_across_fifo_wrap){
  fresh(limits(32768, 2048, 3));
  const uint8_t f = dash::TUNABLE | dash::MUTABLE;
  TopicId a = dash::publish("/T/a", ValueType::Boolean, "boolean", nullptr, f);
  TopicId b = dash::publish("/T/b", ValueType::Boolean, "boolean", nullptr, f);
  TopicId c = dash::publish("/T/c", ValueType::Boolean, "boolean", nullptr, f);
  uint8_t one = 1;
  g_drained.clear();
  dash::detail::pushWrite(a, 1, &one, 1);
  dash::detail::pushWrite(b, 1, &one, 1);
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 2);    // head now at 2
  dash::detail::pushWrite(c, 1, &one, 1);                   // index 2
  dash::detail::pushWrite(a, 1, &one, 1);                   // wraps to index 0
  dash::detail::pushWrite(b, 1, &one, 1);                   // index 1
  dash::unpublish(a);
  g_drained.clear();
  EXPECT_TRUE(dash::drainWrites(collect, nullptr) == 2);
  EXPECT_TRUE(g_drained.size() == 2 && g_drained[0].first == c && g_drained[1].first == b);
  dash::detail::resetForTest();
}

// Free slots are taken round-robin, so republishing one topic does not
// immediately reuse the slot (and soon the generation) of a stale id.
TEST_CASE(dashboard_republish_does_not_reuse_the_same_slot_first){
  fresh(limits(32768, 2048, 4));
  TopicId a = dash::publish("/T/a", ValueType::Boolean, "boolean", nullptr, 0);
  dash::unpublish(a);
  next();                                                   // 'R' frees the slot
  TopicId b = dash::publish("/T/a", ValueType::Boolean, "boolean", nullptr, 0);
  EXPECT_TRUE((b & 0xFF) != (a & 0xFF));
  dash::detail::resetForTest();
}
