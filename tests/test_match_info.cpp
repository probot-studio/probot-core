#include "test_harness.hpp"

#include <probot/ds.hpp>
#include <probot/robot/match_info.hpp>

#include <string>

static_assert(PROBOT_HAS_DS_MATCH_INFO == 1, "feature macro for sketches and SDKs");

namespace robot = probot::robot;
namespace ds = probot::ds;
using robot::AllianceStation;
using robot::MatchInfo;
using robot::MatchType;

namespace {
  std::string json(const MatchInfo& m, uint32_t seq) {
    char buf[robot::kMatchInfoJsonMax + 1];
    size_t n = robot::matchInfoJson(m, seq, buf, sizeof buf);
    return std::string(buf, n);
  }
}

TEST_CASE(match_form_sets_every_field){
  MatchInfo m;
  EXPECT_TRUE(robot::applyMatchForm(
      "station=5&type=2&match=12&replay=3&event=Istanbul%20Regional&gd=LRL", m) == nullptr);
  EXPECT_TRUE(m.station == AllianceStation::Blue2);
  EXPECT_TRUE(m.type == MatchType::Qualification);
  EXPECT_TRUE((m.matchNumber) == (12));
  EXPECT_TRUE((m.replayNumber) == (3));
  EXPECT_TRUE(std::string(m.eventName) == "Istanbul Regional");
  EXPECT_TRUE(std::string(m.gameData) == "LRL");
}

TEST_CASE(match_form_keeps_missing_fields_and_ignores_unknown_keys){
  MatchInfo m;
  EXPECT_TRUE(robot::applyMatchForm("station=1&gd=ABC&event=Final", m) == nullptr);
  EXPECT_TRUE(robot::applyMatchForm("gd=&future=1&&flag&", m) == nullptr);
  EXPECT_TRUE(m.station == AllianceStation::Red1);
  EXPECT_TRUE(std::string(m.gameData).empty());
  EXPECT_TRUE(std::string(m.eventName) == "Final");
  EXPECT_TRUE(robot::applyMatchForm("", m) == nullptr);
  EXPECT_TRUE(robot::applyMatchForm(nullptr, m) == nullptr);
  EXPECT_TRUE(m.station == AllianceStation::Red1);
}

TEST_CASE(match_form_rejects_invalid_values_and_changes_nothing){
  MatchInfo m;
  EXPECT_TRUE(robot::applyMatchForm("station=3&gd=X", m) == nullptr);
  const MatchInfo before = m;
  const char* bad[] = {
    "station=7", "station=-1", "station=", "station=1x", "station= 1",
    "type=4", "match=65536", "match=99999999999", "replay=256", "replay=+1",
    "gd=%ZZ", "gd=%00", "gd=a%0Ab", "gd=%7F", "gd=%C3", "gd=%C0%AF", "gd=%ED%A0%80",
    "gd=%F4%90%80%80", "gd=%F0%80%80%80", "gd=%C2%85", "gd=%C2%9F", "event=%09",
    "gd=ok&station=9",             // a later error discards earlier fields
  };
  for (const char* form : bad) {
    MatchInfo t = m;
    const char* err = robot::applyMatchForm(form, t);
    EXPECT_TRUE(err != nullptr);
    EXPECT_TRUE(t == before);
  }
}

TEST_CASE(match_form_text_limit_counts_utf8_bytes){
  MatchInfo m;
  std::string ok64(64, 'a');
  EXPECT_TRUE(robot::applyMatchForm(("gd=" + ok64).c_str(), m) == nullptr);
  EXPECT_TRUE((strlen(m.gameData)) == (64u));
  EXPECT_TRUE(robot::applyMatchForm(("gd=" + ok64 + "a").c_str(), m) != nullptr);

  // 32 x "ç" = 64 bytes, all percent-encoded (192 raw characters).
  std::string enc;
  for (int i = 0; i < 32; ++i) enc += "%C3%A7";
  EXPECT_TRUE(robot::applyMatchForm(("event=" + enc).c_str(), m) == nullptr);
  EXPECT_TRUE((strlen(m.eventName)) == (64u));
  EXPECT_TRUE(robot::applyMatchForm(("event=" + enc + "%C3%A7").c_str(), m) != nullptr);
  EXPECT_TRUE(robot::applyMatchForm("event=ma%C3%A7%20%F0%9F%A4%96%C2%A0", m) == nullptr);
  EXPECT_TRUE(std::string(m.eventName) == "maç \xF0\x9F\xA4\x96\xC2\xA0");
}

TEST_CASE(match_info_json_escapes_text_and_fits_the_worst_case){
  MatchInfo m;
  EXPECT_TRUE(json(m, 0) ==
              "{\"seq\":0,\"station\":0,\"type\":0,\"match\":0,\"replay\":0,\"event\":\"\",\"gd\":\"\"}");
  EXPECT_TRUE(robot::applyMatchForm("station=6&type=3&match=65535&replay=255&event=a%22b%5Cc&gd=%22", m) == nullptr);
  EXPECT_TRUE(json(m, 4294967295u) ==
              "{\"seq\":4294967295,\"station\":6,\"type\":3,\"match\":65535,\"replay\":255,"
              "\"event\":\"a\\\"b\\\\c\",\"gd\":\"\\\"\"}");

  memset(m.eventName, '"', 64); m.eventName[64] = '\0';
  memset(m.gameData, '\\', 64); m.gameData[64] = '\0';
  std::string worst = json(m, 4294967295u);
  EXPECT_TRUE(!worst.empty());
  EXPECT_TRUE(worst.size() <= robot::kMatchInfoJsonMax);

  char small[64];
  EXPECT_TRUE((robot::matchInfoJson(m, 1, small, sizeof small)) == (0u));
  char exact[robot::kMatchInfoJsonMax + 1];
  size_t n = robot::matchInfoJson(m, 4294967295u, exact, worst.size() + 1);
  EXPECT_TRUE((n) == (worst.size()));
  EXPECT_TRUE((robot::matchInfoJson(m, 4294967295u, exact, worst.size())) == (0u));
}

TEST_CASE(match_store_counts_only_real_changes){
  auto& st = robot::matchStore();
  st.resetForTest();
  EXPECT_TRUE((ds::matchInfoSeq()) == (0u));
  EXPECT_TRUE(ds::allianceStation() == AllianceStation::Unknown);

  MatchInfo m;
  st.set(m);                                    // same as the boot state
  EXPECT_TRUE((ds::matchInfoSeq()) == (0u));

  EXPECT_TRUE(robot::applyMatchForm("station=4&gd=RLR&event=Cup&match=7", m) == nullptr);
  st.set(m);
  EXPECT_TRUE((ds::matchInfoSeq()) == (1u));
  st.set(m);
  EXPECT_TRUE((ds::matchInfoSeq()) == (1u));
  EXPECT_TRUE(ds::allianceStation() == AllianceStation::Blue1);
  EXPECT_TRUE((ds::matchInfo().matchNumber) == (7));
  EXPECT_TRUE(std::string(ds::matchInfo().eventName) == "Cup");

  // Bytes after the terminator do not count as a change.
  MatchInfo same = m;
  same.gameData[5] = 'z';
  st.set(same);
  EXPECT_TRUE((ds::matchInfoSeq()) == (1u));

  EXPECT_TRUE(robot::applyMatchForm("gd=", m) == nullptr);
  st.set(m);
  EXPECT_TRUE((ds::matchInfoSeq()) == (2u));
  uint32_t seq = 0;
  EXPECT_TRUE(std::string(st.read(&seq).gameData).empty());
  EXPECT_TRUE((seq) == (2u));

  // The counter skips 0 when it wraps: 0 always means never set since boot.
  st.resetForTest(0xFFFFFFFFu);
  st.set(m);
  EXPECT_TRUE((ds::matchInfoSeq()) == (1u));
  st.resetForTest();
}

TEST_CASE(ds_game_data_copies_like_snprintf){
  auto& st = robot::matchStore();
  st.resetForTest();
  MatchInfo m;
  EXPECT_TRUE(robot::applyMatchForm("gd=ABCDEF", m) == nullptr);
  st.set(m);

  char out[16];
  EXPECT_TRUE((ds::gameData(out, sizeof out)) == (6u));
  EXPECT_TRUE(std::string(out) == "ABCDEF");
  char shortBuf[4];
  EXPECT_TRUE((ds::gameData(shortBuf, sizeof shortBuf)) == (6u));
  EXPECT_TRUE(std::string(shortBuf) == "ABC");
  EXPECT_TRUE((ds::gameData(nullptr, 0)) == (6u));
  st.resetForTest();
  EXPECT_TRUE((ds::gameData(out, sizeof out)) == (0u));
  EXPECT_TRUE(out[0] == '\0');
}
