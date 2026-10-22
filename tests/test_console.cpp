#include "test_harness.hpp"

#include <probot/telemetry/console.hpp>

#include <stdlib.h>
#include <string>
#include <vector>

extern unsigned long _test_millis_now;

namespace con = probot::console;
namespace cd = probot::console::detail;

namespace {
  void resetConsole(size_t bytes = 0) {
    free(cd::g_ring.data);
    cd::g_ring = cd::Ring{};
    cd::g_noMemory = false;
    if (bytes) cd::reserve(bytes);
  }

  struct Line { uint32_t seq, ms; uint8_t level; std::string text; };
  struct Frame { uint32_t boot = 0, firstSeq = 0; std::vector<Line> lines; };

  uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

  Frame decode(const uint8_t* f, size_t n) {
    Frame out;
    if (n < cd::kFrameHead || f[0] != 'L') return out;
    out.boot = le32(f + 1);
    out.firstSeq = le32(f + 5);
    for (size_t i = cd::kFrameHead; i + cd::kWireRecordHead <= n;) {
      size_t len = f[i + 9] | (f[i + 10] << 8);
      out.lines.push_back({le32(f + i), le32(f + i + 4), f[i + 8],
                           std::string((const char*)f + i + cd::kWireRecordHead, len)});
      i += cd::kWireRecordHead + len;
    }
    return out;
  }

  // Every line from the cursor on, across as many frames as it takes.
  std::vector<Line> drain(cd::Cursor& c, size_t cap = 1400) {
    std::vector<Line> all;
    std::vector<uint8_t> buf(cap);
    for (size_t n; (n = cd::buildFrame(c, buf.data(), buf.size())) != 0;) {
      auto f = decode(buf.data(), n);
      all.insert(all.end(), f.lines.begin(), f.lines.end());
    }
    return all;
  }
}

TEST_CASE(console_lines_keep_level_time_and_order){
  resetConsole();
  _test_millis_now = 1234;
  con::info("speed=%d", 3);
  con::warning("low battery");
  con::print(con::Level::Error, "arm stalled\n");   // trailing newline dropped
  uint8_t buf[1400];
  cd::Cursor c;
  auto f = decode(buf, cd::buildFrame(c, buf, sizeof buf));
  EXPECT_TRUE(f.boot != 0 && f.firstSeq == 1 && f.lines.size() == 3);
  EXPECT_TRUE(f.lines[0].seq == 1 && f.lines[0].level == 0 && f.lines[0].text == "speed=3" && f.lines[0].ms == 1234);
  EXPECT_TRUE(f.lines[1].level == 1 && f.lines[2].level == 2 && f.lines[2].text == "arm stalled");
  EXPECT_TRUE(cd::buildFrame(c, buf, sizeof buf) == 0);   // nothing new
  con::info("again");
  f = decode(buf, cd::buildFrame(c, buf, sizeof buf));
  EXPECT_TRUE(f.lines.size() == 1 && f.lines[0].seq == 4);
  _test_millis_now = 0;
}

TEST_CASE(console_overwrites_the_oldest_lines_when_full){
  resetConsole(2048);
  for (int i = 0; i < 200; ++i) con::info("line %03d %s", i, "0123456789012345678901234567890");
  cd::Cursor c;
  auto lines = drain(c);
  EXPECT_TRUE(!lines.empty() && lines.back().seq == 200 && lines.back().text.rfind("line 199", 0) == 0);
  EXPECT_TRUE(lines.front().seq == cd::g_ring.firstSeq && cd::g_ring.firstSeq > 1);
  for (size_t i = 1; i < lines.size(); ++i) EXPECT_TRUE(lines[i].seq == lines[i - 1].seq + 1);

  // A reader that fell behind the overwritten lines restarts at the oldest.
  cd::Cursor old;
  old.seq = 2;
  EXPECT_TRUE(drain(old).front().seq == cd::g_ring.firstSeq);
}

TEST_CASE(console_splits_long_text_and_prefers_newlines){
  resetConsole();
  con::write(con::Level::Info, std::string(1100, 'a').c_str(), 1100);
  std::string twoPart = std::string(300, 'x') + "\n" + std::string(400, 'y');
  con::write(con::Level::Warning, twoPart.c_str(), twoPart.size());
  cd::Cursor c;
  auto lines = drain(c);
  EXPECT_TRUE(lines.size() == 5);
  EXPECT_TRUE(lines[0].text.size() == 512 && lines[1].text.size() == 512 && lines[2].text.size() == 76);
  EXPECT_TRUE(lines[3].text == std::string(300, 'x') && lines[4].text == std::string(400, 'y'));
}

TEST_CASE(console_printf_is_not_limited_to_the_stack_buffer){
  resetConsole();
  std::string big(400, 'p');
  con::error("%s|end", big.c_str());
  std::string huge(700, 'q');
  con::info("%s", huge.c_str());                  // capped at one 512-byte line
  cd::g_scratchBusy = 1;                           // another task holds the scratch
  con::info("%s", big.c_str());
  cd::g_scratchBusy = 0;
  cd::Cursor c;
  auto lines = drain(c);
  EXPECT_TRUE(lines.size() == 3);
  EXPECT_TRUE(lines[0].text == big + "|end" && lines[0].level == 2);
  EXPECT_TRUE(lines[1].text == huge.substr(0, 512));
  EXPECT_TRUE(lines[2].text == big.substr(0, 159));
}

TEST_CASE(console_resize_keeps_the_newest_lines_and_sequence){
  resetConsole();                                  // lazy default ring
  for (int i = 0; i < 80; ++i) con::info("boot line %02d with some padding text", i);
  uint32_t next = cd::g_ring.nextSeq, boot = cd::g_ring.boot;
  EXPECT_TRUE(cd::g_ring.cap == cd::kDefaultBytes && cd::g_ring.firstSeq == 1);
  EXPECT_TRUE(cd::reserve(2048));
  EXPECT_TRUE(cd::g_ring.cap == 2048 && cd::g_ring.nextSeq == next && cd::g_ring.boot == boot);
  EXPECT_TRUE(cd::g_ring.firstSeq > 1);
  cd::Cursor c;
  auto lines = drain(c);
  EXPECT_TRUE(lines.back().text == "boot line 79 with some padding text");
  EXPECT_TRUE(!cd::reserve(1024) && !cd::reserve(65537));
}

TEST_CASE(console_frames_fit_small_buffers_and_resend_is_one_shot){
  resetConsole();
  con::info("%s", std::string(500, 'z').c_str());
  con::info("short");
  std::vector<uint8_t> small(cd::kMinFrame);
  cd::Cursor c;
  auto f = decode(small.data(), cd::buildFrame(c, small.data(), small.size()));
  EXPECT_TRUE(f.lines.size() == 1 && f.lines[0].text.size() == 500);
  EXPECT_TRUE(cd::buildFrame(c, small.data(), cd::kMinFrame - 1) == 0);   // too small: nothing
  EXPECT_TRUE(!cd::takeResend());
  cd::requestResend();
  EXPECT_TRUE(cd::takeResend() && !cd::takeResend());
}

TEST_CASE(console_cursor_survives_a_resize_and_a_bogus_sequence){
  resetConsole();
  for (int i = 0; i < 100; ++i) con::info("wrap line %03d padded to make the ring wrap", i);
  cd::Cursor c;
  std::vector<uint8_t> buf(cd::kMinFrame);
  cd::buildFrame(c, buf.data(), buf.size());       // reader part way through
  EXPECT_TRUE(c.seq > cd::g_ring.firstSeq && c.seq < cd::g_ring.nextSeq);
  EXPECT_TRUE(cd::reserve(8192));                   // records move
  auto lines = drain(c);
  EXPECT_TRUE(!lines.empty() && lines.front().seq == cd::g_ring.firstSeq);
  for (auto& l : lines) {
    char want[64];
    snprintf(want, sizeof want, "wrap line %03u padded to make the ring wrap", (unsigned)(l.seq - 1));
    EXPECT_TRUE(l.text == want);
  }

  cd::Cursor ahead;                                 // a sequence the ring never issued
  ahead.seq = cd::g_ring.nextSeq + 5;
  ahead.gen = cd::g_ring.gen;
  EXPECT_TRUE(drain(ahead).front().seq == cd::g_ring.firstSeq);
}

TEST_CASE(console_record_header_wraps_the_ring_end){
  resetConsole(2048);
  std::string body(504, 'h');                       // 511-byte records
  for (int i = 0; i < 4; ++i) con::print(con::Level::Info, body.c_str());
  EXPECT_TRUE(cd::g_ring.head == 2044);             // next header straddles the end
  con::warning("across the end");
  cd::Cursor c;
  auto lines = drain(c);
  EXPECT_TRUE(lines.size() == 4 && lines.front().seq == 2);
  EXPECT_TRUE(lines.back().text == "across the end" && lines.back().level == 1);
}

TEST_CASE(console_never_splits_a_utf8_character){
  resetConsole();
  std::string s = "a";
  for (int i = 0; i < 300; ++i) s += "\xC5\x9F";      // U+015F, 2 bytes
  con::write(con::Level::Info, s.c_str(), s.size());   // 601 bytes: split
  con::info("%s", s.c_str());                          // capped
  std::string t;
  for (int i = 0; i < 100; ++i) t += "\xC5\x9F";
  cd::g_scratchBusy = 1;
  con::info("%s", t.c_str());                          // 159-byte fallback
  cd::g_scratchBusy = 0;
  cd::Cursor c;
  auto lines = drain(c);
  EXPECT_TRUE(lines.size() == 4);
  EXPECT_TRUE(lines[0].text.size() == 511 && lines[0].text + lines[1].text == s);
  EXPECT_TRUE(lines[2].text.size() == 511);
  EXPECT_TRUE(lines[3].text.size() == 158);
  EXPECT_TRUE(cd::utf8Cut("\xE2\x82\xAC", 3) == 3 && cd::utf8Cut("\xE2\x82\xAC", 2) == 0);
}

TEST_CASE(console_empty_text_and_lazy_allocation_failure){
  resetConsole();
  con::write(con::Level::Info, "\n\n", 2);          // one empty line
  con::write(con::Level::Info, "", 0);              // one empty line
  cd::Cursor c;
  auto lines = drain(c);
  EXPECT_TRUE(lines.size() == 2 && lines[0].text.empty() && lines[1].text.empty());

  resetConsole();
  cd::g_noMemory = true;                            // the lazy allocation failed once
  con::info("dropped");
  EXPECT_TRUE(cd::g_ring.data == nullptr);
  cd::g_noMemory = false;
}
