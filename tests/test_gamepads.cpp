#include "test_harness.hpp"
#include "gamepad_test_util.hpp"

#include <probot/ds.hpp>
#include <probot/io/gamepad.hpp>
#include <probot/io/joystick_api.hpp>
#include <probot/io/joystick_frame.hpp>

#include <string.h>
#include <string>
#include <vector>

extern unsigned long _test_millis_now;

namespace io = probot::io;
namespace mapping = probot::io::joystick_mapping;

namespace {
  void resetPads() { resetGamepadsForTest(); }

  // 'J' frame bytes: header, big-endian axes, packed buttons, then the
  // optional value trailer when `values` is not null.
  std::vector<uint8_t> jFrame(uint8_t port, std::vector<int16_t> axes, std::vector<bool> buttons,
                              const std::vector<uint8_t>* values = nullptr) {
    std::vector<uint8_t> f = {0x4A, (uint8_t)axes.size(), (uint8_t)buttons.size(), port};
    for (int16_t a : axes) { f.push_back((uint8_t)((uint16_t)a >> 8)); f.push_back((uint8_t)a); }
    std::vector<uint8_t> bits((buttons.size() + 7) / 8, 0);
    for (size_t i = 0; i < buttons.size(); ++i) if (buttons[i]) bits[i / 8] |= (uint8_t)(1 << (i % 8));
    f.insert(f.end(), bits.begin(), bits.end());
    if (values) { f.push_back((uint8_t)values->size()); f.insert(f.end(), values->begin(), values->end()); }
    return f;
  }
}

TEST_CASE(joystick_frame_decodes_port_axes_buttons_and_values){
  std::vector<uint8_t> vals = {0, 255, 128};
  auto f = jFrame(3, {32767, -32767}, {true, false, true}, &vals);
  io::frame::Joystick d;
  EXPECT_TRUE(io::frame::decodeJoystick(f.data(), f.size(), d));
  EXPECT_TRUE(d.port == 3 && d.axisCount == 2 && d.buttonCount == 3 && d.valueCount == 3);
  EXPECT_TRUE(d.axes[0] == 1.0f && d.axes[1] == -1.0f);
  EXPECT_TRUE(d.buttons[0] && !d.buttons[1] && d.buttons[2]);
  EXPECT_TRUE(d.values[1] == 255 && d.values[2] == 128);

  auto old = jFrame(0, {0}, {true});            // pre-port, pre-value page
  EXPECT_TRUE(io::frame::decodeJoystick(old.data(), old.size(), d) && d.port == 0 && d.valueCount == 0);
  std::vector<uint8_t> none;
  auto empty = jFrame(1, {0}, {true}, &none);   // trailer with a count of 0
  EXPECT_TRUE(io::frame::decodeJoystick(empty.data(), empty.size(), d) && d.valueCount == 0);
}

TEST_CASE(joystick_frame_rejects_malformed_input){
  io::frame::Joystick d;
  auto badPort = jFrame(6, {0}, {false});
  EXPECT_TRUE(!io::frame::decodeJoystick(badPort.data(), badPort.size(), d));
  std::vector<uint8_t> tooMany = {0, 0, 0};    // more values than buttons
  auto f = jFrame(0, {}, {true, true}, &tooMany);
  EXPECT_TRUE(!io::frame::decodeJoystick(f.data(), f.size(), d));
  std::vector<uint8_t> two = {1, 2};
  auto g = jFrame(0, {}, {true, true}, &two);
  g.pop_back();                                 // trailer shorter than its count
  EXPECT_TRUE(!io::frame::decodeJoystick(g.data(), g.size(), d));
  g.push_back(2); g.push_back(9);               // trailing junk
  EXPECT_TRUE(!io::frame::decodeJoystick(g.data(), g.size(), d));
  uint8_t counts[4] = {0x4A, 21, 0, 0};
  EXPECT_TRUE(!io::frame::decodeJoystick(counts, sizeof counts, d));
  std::vector<uint8_t> twenty(20, 7);
  auto big = jFrame(0, std::vector<int16_t>(20, 0), std::vector<bool>(20, true), &twenty);
  EXPECT_TRUE(big.size() == io::frame::kMaxSize && io::frame::decodeJoystick(big.data(), big.size(), d));
}

TEST_CASE(info_frame_decodes_and_rejects_bad_lengths){
  const char name[] = "Xbox Wireless Controller";
  std::vector<uint8_t> f = {0x4E, 1, 1, (uint8_t)strlen(name)};
  f.insert(f.end(), name, name + strlen(name));
  io::frame::Info d;
  EXPECT_TRUE(io::frame::decodeInfo(f.data(), f.size(), d));
  EXPECT_TRUE(d.port == 1 && d.standard && d.nameLen == strlen(name) && memcmp(d.name, name, d.nameLen) == 0);
  f.push_back('x');
  EXPECT_TRUE(!io::frame::decodeInfo(f.data(), f.size(), d));
  std::vector<uint8_t> longName(4 + 64, 'a');
  longName[0] = 0x4E; longName[1] = 0; longName[2] = 0; longName[3] = 64;
  EXPECT_TRUE(!io::frame::decodeInfo(longName.data(), longName.size(), d));
  uint8_t badPort[4] = {0x4E, 6, 0, 0};
  EXPECT_TRUE(!io::frame::decodeInfo(badPort, sizeof badPort, d));
  uint8_t shortF[3] = {0x4E, 0, 0}, magic[4] = {0x4A, 0, 0, 0};
  EXPECT_TRUE(!io::frame::decodeInfo(shortF, sizeof shortF, d) && !io::frame::decodeInfo(magic, sizeof magic, d));
  longName.pop_back(); longName[3] = 63;
  EXPECT_TRUE(io::frame::decodeInfo(longName.data(), longName.size(), d) && d.nameLen == 63);
}

TEST_CASE(gamepad_ports_are_independent_and_gated){
  resetPads();
  _test_millis_now = 1000;
  float ax[1] = {0.5f}; bool bt[1] = {true};
  io::detail::applyFrame(2, 1000, true, ax, 1, bt, 1);
  EXPECT_TRUE(io::gamepad(2).read().axes[0] == 0.5f);
  EXPECT_TRUE(io::gamepad(0).read().axisCount == 0);
  EXPECT_TRUE(io::rawGamepad(2).read().buttons[0]);

  io::detail::applyFrame(3, 1000, false, ax, 1, bt, 1);   // disabled: raw only
  EXPECT_TRUE(io::gamepad(3).read().axisCount == 0);
  EXPECT_TRUE(io::rawGamepad(3).read().axes[0] == 0.5f);
  io::GamepadSnapshot s;
  EXPECT_TRUE(probot::ds::rawGamepad(3, s) && s.axes[0] == 0.5f);
  EXPECT_TRUE(!probot::ds::rawGamepad(6, s));

  io::detail::applyFrame(9, 1000, true, ax, 1, bt, 1);    // no such port
  EXPECT_TRUE(io::gamepad(9).read().axisCount == 0);
  EXPECT_TRUE(io::detail::newestFrameMs() == 1000);

  io::detail::neutralizeAll(1001, false);
  EXPECT_TRUE(io::gamepad(2).read().axisCount == 0);
  EXPECT_TRUE(io::rawGamepad(2).read().axisCount == 1);
  io::detail::neutralizeAll(1002, true);
  EXPECT_TRUE(io::rawGamepad(2).read().axisCount == 0);
  EXPECT_TRUE(io::detail::newestFrameMs() == 1000);        // neutral writes are not frames
  EXPECT_TRUE(io::gamepad(-1).read().axisCount == 0 && io::gamepad(256).read().axisCount == 0);
  _test_millis_now = 0;
}

TEST_CASE(raw_gamepad_reports_only_ports_that_received_a_frame){
  resetPads();
  io::detail::neutralizeAll(0, true);
  io::GamepadSnapshot s;
  EXPECT_TRUE(!probot::ds::rawGamepad(5, s));   // owner release writes are not frames
  bool bt[1] = {true};
  io::detail::applyFrame(5, 0, false, nullptr, 0, bt, 1);
  EXPECT_TRUE(probot::ds::rawGamepad(5, s) && s.buttons[0]);
}

TEST_CASE(trigger_axes_use_analog_values_when_sent){
  resetPads();
  _test_millis_now = 50;
  mapping::setActive(&mapping::mapping_standard());
  bool bt[8] = {false, false, false, false, false, false, true, true};
  uint8_t vals[8] = {0, 0, 0, 0, 0, 0, 51, 255};
  io::detail::applyFrame(1, 50, true, nullptr, 0, bt, 8, vals, 8);
  auto js = io::joystick_api::makeForPort(1);
  EXPECT_TRUE(js.getLeftTriggerAxis() == 51 / 255.0f);
  EXPECT_TRUE(js.getRightTriggerAxis() == 1.0f);

  uint8_t rest[8] = {0, 0, 0, 0, 0, 0, 15, 255};          // ~0.06 resting noise
  io::detail::applyFrame(1, 50, true, nullptr, 0, bt, 8, rest, 8);
  EXPECT_TRUE(js.getLeftTriggerAxis() == 0.0f && js.getRawButtonValue(6) == 15 / 255.0f);

  io::detail::applyFrame(1, 50, true, nullptr, 0, bt, 8);  // no values: pressed = 1
  EXPECT_TRUE(js.getLeftTriggerAxis() == 1.0f);
  EXPECT_TRUE(js.getRawButtonValue(0) == 0.0f && js.getRawButtonValue(42) == 0.0f);
  mapping::setActive(&mapping::mapping_logitech_f310());
  _test_millis_now = 0;
}

TEST_CASE(pov_reports_eight_directions_from_dpad_buttons){
  resetPads();
  _test_millis_now = 10;
  mapping::setActive(&mapping::mapping_standard());
  auto js = io::joystick_api::makeDefault();
  auto press = [](std::vector<int> on) {
    bool bt[16] = {};
    for (int i : on) bt[i] = true;
    io::detail::applyFrame(0, 10, true, nullptr, 0, bt, 16);
  };
  const int up = 12, down = 13, left = 14, right = 15;
  press({});              EXPECT_TRUE(js.getPOV() == -1);
  press({up});            EXPECT_TRUE(js.getPOV() == 0);
  press({up, right});     EXPECT_TRUE(js.getPOV() == 45);
  press({down, right});   EXPECT_TRUE(js.getPOV() == 135);
  press({down, left});    EXPECT_TRUE(js.getPOV() == 225);
  press({up, left});      EXPECT_TRUE(js.getPOV() == 315);
  press({up, down});      EXPECT_TRUE(js.getPOV() == -1);
  press({up, down, left});EXPECT_TRUE(js.getPOV() == 270);
  // Directions hold on their diagonals, as before when up won the tie.
  press({up, right});
  EXPECT_TRUE(js.getDpadUp() && js.getDpadRight() && !js.getDpadDown() && !js.getDpadLeft());
  mapping::setActive(&mapping::mapping_logitech_f310());
  _test_millis_now = 0;
}

TEST_CASE(gamepad_name_is_reported_while_frames_arrive){
  resetPads();
  _test_millis_now = 100;
  auto js = io::joystick_api::makeForPort(4);
  char name[80];
  EXPECT_TRUE(!js.getName(name, sizeof name) && name[0] == '\0');
  std::string longName(70, 'g');
  io::detail::setInfo(4, longName.data(), longName.size(), true);
  bool bt[1] = {false};
  io::detail::applyFrame(4, 100, true, nullptr, 0, bt, 1);
  EXPECT_TRUE(js.getName(name, sizeof name) && strlen(name) == io::kMaxGamepadName);
  EXPECT_TRUE(js.isStandardMapping());
  char small[5];
  EXPECT_TRUE(js.getName(small, sizeof small) && strcmp(small, "gggg") == 0);
  // While disabled the runtime rewrites the gated ports with neutral data
  // every pass; that must not keep a departed gamepad's name alive.
  for (uint32_t t = 200; t <= 700; t += 100) { _test_millis_now = t; io::detail::neutralizeAll(t, false); }
  EXPECT_TRUE(!js.getName(name, sizeof name) && !js.isStandardMapping());

  _test_millis_now = 800;
  io::detail::applyFrame(4, 800, true, nullptr, 0, bt, 1);
  EXPECT_TRUE(js.getName(name, sizeof name));
  io::detail::neutralizeAll(801, true);                     // owner released
  io::detail::applyFrame(4, 802, true, nullptr, 0, bt, 1);  // new client, no 'N' yet
  EXPECT_TRUE(!js.getName(name, sizeof name));
  _test_millis_now = 0;
}

TEST_CASE(options_is_the_home_button_in_the_standard_profiles){
  resetPads();
  _test_millis_now = 20;
  const mapping::MappingProfile* profiles[] = {
    &mapping::mapping_logitech_f310(), &mapping::mapping_standard(), &mapping::mapping_axis9_dpad(),
  };
  for (const auto* m : profiles) {
    mapping::setActive(m);
    auto js = io::joystick_api::makeDefault();
    bool bt[17] = {};
    bt[16] = true;
    io::detail::applyFrame(0, 20, true, nullptr, 0, bt, 17);
    EXPECT_TRUE(js.getOptions() && !js.getBack() && !js.getStart());
    bt[16] = false;
    io::detail::applyFrame(0, 20, true, nullptr, 0, bt, 17);
    EXPECT_TRUE(!js.getOptions());
    // A pad without a Home button (16 buttons) never reads it.
    bool pad16[16] = {};
    io::detail::applyFrame(0, 20, true, nullptr, 0, pad16, 16);
    EXPECT_TRUE(!js.getOptions());
  }
  EXPECT_TRUE(mapping::findByName("xbox")->Options == 16 && mapping::findByName("ps")->Options == 16);
  EXPECT_TRUE(mapping::mapping_tuna_default().Options == 10);
  mapping::setActive(&mapping::mapping_logitech_f310());
  _test_millis_now = 0;
}
