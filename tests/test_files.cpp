#include "test_harness.hpp"

#include <probot/files/files.hpp>

#include <string>

static_assert(PROBOT_HAS_FILES_SHARE == 1, "feature macro for sketches and SDKs");

namespace pf = probot::files;
namespace fd = probot::files::detail;

TEST_CASE(files_names_from_the_network_cannot_leave_the_shared_folder){
  EXPECT_TRUE(fd::validFileName("FRC_20261022_101500.wpilog"));
  EXPECT_TRUE(fd::validFileName("maç kaydı.txt"));               // UTF-8 and spaces
  EXPECT_TRUE(fd::validFileName("..hidden"));
  EXPECT_TRUE(fd::validFileName("..."));                         // a plain name, not a parent
  EXPECT_TRUE(fd::validFileName(".. "));
  EXPECT_TRUE(!fd::validFileName(""));
  EXPECT_TRUE(!fd::validFileName(nullptr));
  EXPECT_TRUE(!fd::validFileName("."));
  EXPECT_TRUE(!fd::validFileName(".."));
  EXPECT_TRUE(!fd::validFileName("../secret"));
  EXPECT_TRUE(!fd::validFileName("sub/file"));
  EXPECT_TRUE(!fd::validFileName("sub\\file"));
  EXPECT_TRUE(!fd::validFileName("a\nb"));
  EXPECT_TRUE(fd::validFileName(std::string(127, 'a').c_str()));
  EXPECT_TRUE(!fd::validFileName(std::string(128, 'a').c_str()));
}

TEST_CASE(files_share_names_and_folders_are_validated){
  EXPECT_TRUE(fd::validShareName("logs"));
  EXPECT_TRUE(fd::validShareName("sd_card-1"));
  EXPECT_TRUE(fd::validShareName("abcdefghijklmno"));          // 15
  EXPECT_TRUE(!fd::validShareName("abcdefghijklmnop"));        // 16
  EXPECT_TRUE(!fd::validShareName(""));
  EXPECT_TRUE(!fd::validShareName("my logs"));
  EXPECT_TRUE(!fd::validShareName("a/b"));

  EXPECT_TRUE(fd::validDir("/"));
  EXPECT_TRUE(fd::validDir("/logs"));
  EXPECT_TRUE(fd::validDir("/logs/"));
  EXPECT_TRUE(fd::validDir("/a/.b/c"));
  EXPECT_TRUE(!fd::validDir("logs"));
  EXPECT_TRUE(!fd::validDir("/logs/.."));
  EXPECT_TRUE(!fd::validDir("/../x"));
  EXPECT_TRUE(!fd::validDir("/a/./b"));
  EXPECT_TRUE(!fd::validDir(nullptr));
}

TEST_CASE(files_paths_join_without_double_slashes){
  char out[64];
  EXPECT_TRUE(fd::joinPath("/", "a.wpilog", out, sizeof out) && std::string(out) == "/a.wpilog");
  EXPECT_TRUE(fd::joinPath("/logs", "a", out, sizeof out) && std::string(out) == "/logs/a");
  EXPECT_TRUE(fd::joinPath("/logs//", "a", out, sizeof out) && std::string(out) == "/logs/a");
  char small[6];
  EXPECT_TRUE(!fd::joinPath("/logs", "a", small, sizeof small));   // "/logs/a" needs 8
}

TEST_CASE(files_url_decoding_rejects_malformed_and_nul_escapes){
  char out[32];
  EXPECT_TRUE(fd::urlDecode("ma%C3%A7%20kayd%C4%B1.txt", out, sizeof out) &&
              std::string(out) == "maç kaydı.txt");
  EXPECT_TRUE(fd::urlDecode("a+b", out, sizeof out) && std::string(out) == "a+b");
  EXPECT_TRUE(fd::urlDecode("%2e%2E%2Fx", out, sizeof out) && std::string(out) == "../x");
  EXPECT_TRUE(!fd::validFileName(out));                          // decoded, then still refused
  EXPECT_TRUE(!fd::urlDecode("a%2", out, sizeof out));
  EXPECT_TRUE(!fd::urlDecode("a%zz", out, sizeof out));
  EXPECT_TRUE(!fd::urlDecode("a%00b", out, sizeof out));
  char small[4];
  EXPECT_TRUE(fd::urlDecode("abc", small, sizeof small));
  EXPECT_TRUE(!fd::urlDecode("abcd", small, sizeof small));
}

TEST_CASE(files_listing_entries_are_escaped_json){
  char out[128];
  size_t n = fd::jsonFile("a\"b\\c\x01.log", 1234567890123ULL, 1792656000, true, out, sizeof out);
  EXPECT_TRUE(n > 0 && std::string(out, n) ==
              "{\"name\":\"a\\\"b\\\\c\\u0001.log\",\"size\":1234567890123,\"mtime\":1792656000}");
  n = fd::jsonFile("x", 0, -5, false, out, sizeof out);
  EXPECT_TRUE(n > 0 && std::string(out, n) == ",{\"name\":\"x\",\"size\":0,\"mtime\":0}");
  char small[20];
  EXPECT_TRUE(fd::jsonFile("x", 0, 0, true, small, sizeof small) == 0);
  // The longest valid name, all escaped, fits the listing's entry buffer.
  char entry[probot::files::kMaxFileName * 6 + 80];
  std::string quotes(probot::files::kMaxFileName, '"');
  EXPECT_TRUE(fd::validFileName(quotes.c_str()));
  EXPECT_TRUE(fd::jsonFile(quotes.c_str(), UINT64_MAX, INT64_MAX, false, entry, sizeof entry) > 0);
}

TEST_CASE(files_are_offered_only_while_idle_and_never_after_an_estop){
  using probot::robot::Phase;
  using probot::robot::Status;
  EXPECT_TRUE(fd::transferAllowed(Phase::STOPPED, Status::STOP, false));
  EXPECT_TRUE(fd::transferAllowed(Phase::TRANSITION, Status::STOP, false));
  EXPECT_TRUE(!fd::transferAllowed(Phase::STOPPED, Status::STOP, true));       // e-stopped
  EXPECT_TRUE(!fd::transferAllowed(Phase::STOPPED, Status::INIT, false));      // run requested
  EXPECT_TRUE(!fd::transferAllowed(Phase::TELEOP_RUN, Status::STOP, false));   // stopping
  EXPECT_TRUE(!fd::transferAllowed(Phase::AUTO_INIT, Status::INIT, false));
}

TEST_CASE(files_shares_are_bounded_and_replaced_by_name){
  fd::resetForTest();
  int fsA = 0, fsB = 0;
  EXPECT_TRUE(fd::addShare("logs", &fsA, "/logs"));
  EXPECT_TRUE(!fd::addShare("bad name", &fsB, "/"));
  EXPECT_TRUE(!fd::addShare("sd", &fsB, "relative"));
  EXPECT_TRUE(!pf::share("sd", "/sd/../x"));
  EXPECT_TRUE(fd::addShare("sd", &fsB, "/"));
  EXPECT_TRUE(fd::addShare("c", &fsB, "/c"));
  EXPECT_TRUE(fd::addShare("d", &fsB, "/d//"));
  EXPECT_TRUE(!fd::addShare("e", &fsB, "/e"));                  // at most 4
  EXPECT_TRUE(fd::count() == 4);

  fd::Share s{};
  EXPECT_TRUE(fd::findShare("sd", s) && s.fs == &fsB && std::string(s.dir) == "/");
  EXPECT_TRUE(fd::shareAt(0, s) && std::string(s.name) == "logs" && s.fs == &fsA);
  EXPECT_TRUE(fd::findShare("d", s) && std::string(s.dir) == "/d");          // trailing slashes dropped
  EXPECT_TRUE(!fd::findShare("nope", s));
  EXPECT_TRUE(!fd::shareAt(4, s));

  // The same name replaces the entry in place, even when full; a copy
  // taken before is no longer current, so its transfer stops.
  fd::Share old{};
  EXPECT_TRUE(fd::findShare("logs", old) && fd::shareCurrent(old));
  EXPECT_TRUE(pf::share("logs", "/sd/logs/"));                  // VFS path: no fs
  EXPECT_TRUE(fd::count() == 4 && !fd::shareCurrent(old));
  EXPECT_TRUE(fd::shareAt(0, s) && std::string(s.name) == "logs" && s.fs == nullptr &&
              std::string(s.dir) == "/sd/logs" && fd::shareCurrent(s));
  fd::resetForTest();
}

TEST_CASE(files_unshare_removes_a_share_and_frees_its_place){
  fd::resetForTest();
  int fsA = 0;
  EXPECT_TRUE(pf::share("a", "/littlefs/a") && fd::addShare("b", &fsA, "/b") && pf::share("c", "/sd"));
  fd::Share b{};
  EXPECT_TRUE(fd::findShare("b", b));
  EXPECT_TRUE(pf::unshare("b") && !pf::unshare("b") && !pf::unshare(nullptr));
  EXPECT_TRUE(fd::count() == 2 && !fd::shareCurrent(b) && !fd::findShare("b", b));
  fd::Share s{};
  EXPECT_TRUE(fd::shareAt(0, s) && std::string(s.name) == "a");
  EXPECT_TRUE(fd::shareAt(1, s) && std::string(s.name) == "c" && fd::shareCurrent(s));   // order kept
  EXPECT_TRUE(fd::addShare("b", &fsA, "/b") && fd::count() == 3);
  fd::resetForTest();
}

TEST_CASE(files_task_marks_only_current_shares_as_read){
  fd::resetForTest();
  EXPECT_TRUE(pf::share("logs", "/littlefs/logs"));
  fd::Share sh{};
  EXPECT_TRUE(fd::findShare("logs", sh));
  EXPECT_TRUE(fd::enterShare(sh) && fd::g_active == sh.gen);
  fd::leaveShare();
  EXPECT_TRUE(fd::g_active == 0);
  // Replaced or removed before the first file call: nothing is marked, so
  // share() and unshare() do not wait for it.
  EXPECT_TRUE(pf::share("logs", "/sd/logs"));
  EXPECT_TRUE(!fd::enterShare(sh) && fd::g_active == 0);
  EXPECT_TRUE(fd::findShare("logs", sh) && pf::unshare("logs"));
  EXPECT_TRUE(!fd::enterShare(sh) && fd::g_active == 0);
  fd::resetForTest();
}

TEST_CASE(files_busy_reports_a_transfer_that_outlives_unshare){
  fd::resetForTest();
  EXPECT_TRUE(!pf::busy());
  EXPECT_TRUE(pf::share("logs", "/littlefs/logs"));
  fd::Share sh{};
  EXPECT_TRUE(fd::findShare("logs", sh) && fd::enterShare(sh) && pf::busy());
  // unshare() waits at most kReleaseWaitMs (not at all on the host); a
  // transfer blocked on a slow browser can still hold the file after it.
  EXPECT_TRUE(pf::unshare("logs") && pf::busy());
  fd::leaveShare();
  EXPECT_TRUE(!pf::busy());
  fd::resetForTest();
}
