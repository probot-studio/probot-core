# Probot

Türkçe: [README.tr.md](README.tr.md)

Communication library for ESP32-based robotics competitions. The robot hosts
a WiFi access point, serves a browser-based Driver Station, and carries
joystick input to the robot over a low-latency WebSocket (binary frames at
50 Hz) with automatic failsafes: input is zeroed after 500 ms without
joystick data and the robot stops after 10 s of Driver Station silence.

**dev (0.4.0 candidate)** · ESP32 / ESP32-S3 · [Docs](https://probotstudio.com/docs) ·
[API reference](API.md) · [Changelog](CHANGELOG.md)

---

## Supported hardware

- Target: **ESP32-S3**. The library is developed for the ESP32-S3; other
  ESP32 variants may be adaptable but are outside the official support
  scope.
- Recommended board: Boardoza Pulse S32-S3
  ([purchase](https://boardoza.com/product/boardoza-pulse-s32-s3-breakout-board/)).
- Requires the Arduino-ESP32 core **3.x** (the ESP-IDF component metadata
  asks for `espressif/arduino-esp32 >= 3.0.0` and ESP-IDF `>= 5.0`).
- Built-in status LED support depends on the Adafruit NeoPixel library.

## Installation

### Arduino IDE

1. **Library:** Arduino IDE → Library Manager → search for **"probot"** →
   Install. For the latest version instead:
   ```bash
   git clone https://github.com/probot-studio/probot-core ~/Arduino/libraries/probot-core
   ```
2. **ESP32 core:** Boards Manager → "esp32" (Espressif) → version **3.x**
   must be installed.
3. **Board:** `ESP32S3 Dev Module` (or `ESP32 Dev Module`).
4. **Partition scheme:** Tools → Partition Scheme → **Huge APP (3MB No OTA)**.
   This setting is required: the default partition is too small and the
   build will not fit.

### arduino-cli

The repository's Makefile builds the examples with `arduino-cli` (default
board `esp32:esp32:esp32s3`). See [Development](#development-and-testing).

### PlatformIO

```ini
lib_deps = https://github.com/probot-studio/probot-core.git
```

### ESP-IDF with the Arduino component

The repository ships an ESP-IDF component (`CMakeLists.txt`,
`idf_component.yml`; it requires the `arduino` component). Call
`probot::runtime_setup()` from your application.

## Your first robot (5 minutes)

```cpp
#define PROBOT_WIFI_AP_SSID     "MyRobot"
#define PROBOT_WIFI_AP_PASSWORD "robot1234"   // at least 8 characters
#define PROBOT_WIFI_AP_CHANNEL  1             // 1-13 (assign by hand at a competition)
#include <probot.h>

void teleopInit() {}               // once, when INIT is pressed with TeleOp selected

void teleopLoop() {                // called repeatedly (~50 Hz)
  auto js = probot::io::joystick_api::makeDefault();
  float forward = js.getLeftY();   // -1..+1 (forward is positive)
  bool  button  = js.getA();
  // your motor code goes here
  delay(20);
}
void teleopStop() {}               // stop safely every time TeleOp is left

void autonomousInit() {}
void autonomousLoop() { delay(100); }
void autonomousStop() {}           // stop safely every time Auto is left
```

> Do **not** define `setup()` or `loop()`: the library defines them.
> The four safety hooks (`autonomousLoop/Stop`, `teleopLoop/Stop`) are
> mandatory. The `init` hooks are optional; the main skeleton shows them for
> hardware preparation.

1. Upload, then read the IP from the Serial monitor (`192.168.4.1`).
2. Connect a tablet or phone to the `MyRobot` WiFi network. The welcome page
   opens by itself (captive portal).
3. If it does not, open `http://192.168.4.1` in a browser.
4. Connect a gamepad to the tablet (USB/Bluetooth), select the mode, then
   press **Init** and **Start**.

## Examples

| Example | What it does |
|---|---|
| `JoystickTest` | Prints axis/button values to Serial and the telemetry panel. Good first test. |
| `TankDrive` | Two-motor tank drive (BTS7960/IBT-2 style driver). A template for motor code. |
| `ServoTest` | Servo control from the joystick: the correct jitter-free way to drive servos. |

## Configuration macros

All of them are defined **before** the `#include <probot.h>` line.

| Macro | Default | Description |
|---|---|---|
| `PROBOT_WIFI_AP_SSID` | `Probot-XXXXXX` | AP name. If left undefined, a MAC suffix is added automatically. At most 25 characters with the suffix, 32 without |
| `PROBOT_WIFI_AP_PASSWORD` | none (required) | AP password (at least 8 characters) |
| `PROBOT_WIFI_AP_CHANNEL` | none (required) | AP channel, **1-13**. At a competition, give each robot a different channel by hand |
| `PROBOT_WIFI_AUTO_CHANNEL` | `0` | `1`: at boot the robot scans the band and picks the emptiest channel **itself**. **Not recommended for a fleet** (see the channel plan); single-robot use only |
| `PROBOT_WIFI_AP_SSID_MAC_SUFFIX` | off | Appends `-XXXXXX` (MAC) to the SSID |
| `PROBOT_DS_TIMEOUT_MS` | `10000` | Timeout (ms) when data from the Driver Station stops |
| `PROBOT_DS_TIMEOUT_FORCE_STOP` | `1` | `1`: the robot STOPs on timeout. `0`: the loop keeps running, joystick is neutral, and control resumes when the link returns |
| `PROBOT_DS_OWNER_TIMEOUT_MS` | `5000` | How long the owner client may stay silent before its slot is freed |
| `PROBOT_INPUT_TIMEOUT_MS` | `500` | Time after which axes are zeroed when joystick data stops |
| `PROBOT_WIFI_ENABLE_11B` | `0` | `1`: enable 802.11b rates (only for pre-2010 devices; multiplies beacon airtime by 6) |
| `PROBOT_WIFI_PMF_REQUIRED` | `0` | `1`: require PMF (802.11w), protection against deauth spoofing; may be incompatible with older tablets |
| `PROBOT_CAPTIVE_PORTAL` | `1` | A device joining the network opens the welcome page by itself; `0` disables it |
| `NEOPIXEL_PIN` / `NEOPIXEL_COUNT` | `3` / `1` | Status LED pin / count |
| `PROBOT_LOOP_DEADLINE_MS` | `2000` | If an InitLoop/loop pass exceeds this, the robot is "stalled": input zeroed, held halt-safe (no kill, no reboot) |
| `PROBOT_WDT_TIMEOUT_S` | `8` | Hardware watchdog (sysloop only; a stuck *library* lock reboots the chip, user code does not) |
| `PROBOT_ESTOP_ENABLE_PIN` | `-1` | Enable GPIO driven by the library (motor driver enable / contactor). HIGH at boot, LOW on emergency stop |
| `PROBOT_ESTOP_END_MS` | `500` | Time granted to the active OpMode `stop()` on emergency stop; the chip reboots if it is exceeded |
| `PROBOT_RSL_PIN` | `-1` | Robot signal light (RSL) digital pin: blinks while the robot may move, solid otherwise |
| `PROBOT_BATTERY_ADC_PIN` | off | Battery measurement, method 1: midpoint of a voltage divider. **Must be an ADC1 pin (GPIO1-10)**: ADC2 does not work while WiFi is on [PB-E104] |
| `PROBOT_BATTERY_R_TOP_K` / `_R_BOT_K` | none | Divider resistors in kΩ (battery side / GND side). Suggestion for 3S: 100k/22k, which gives 2.27 V at 12.6 V |
| `PROBOT_BATTERY_INA` | off | Battery measurement, method 2: I2C sensor, `219` or `226`. Cannot be combined with the ADC method [PB-E105] |
| `PROBOT_BATTERY_INA_ADDR` | `0x40` | INA I2C address |
| `PROBOT_BATTERY_INA_SDA` / `_SCL` | board default | I2C pins for the INA |
| `PROBOT_BATTERY_TRIM` | `1.0f` | Fine-tuning multiplier against a multimeter (0.5-2.0) |
| `USER_LOOP_PERIOD_MS` | `20` | Call period of InitLoop/loop (~50 Hz) |
| `NEOPIXEL_BRIGHTNESS` | `32` | Status LED brightness (0-255) |

## Competition day: channel plan

- Our defaults are **1, 6, 11**, the classic trio that does not overlap on
  2.4 GHz; automatic channel selection picks the emptiest of these three.
  This is not mandatory: any channel from 1 to 13 can be given with
  `PROBOT_WIFI_AP_CHANNEL`. Giving simultaneously running robots different,
  fixed channels (deterministic assignment) is the safest method for a
  coordinated fleet.
- The channel can be changed on competition day **without reflashing**:
  Logs page → Change Channel. Selections from 1 to 13 are applied live with
  CSA (compatible clients follow without dropping) and saved persistently.
  Do not change it **during** a match: some tablets do not follow CSA and
  may drop for a few seconds.
- Some laptops and tablets **cannot see channels 12-13** because of regional
  locks. If a device cannot find a robot, give that robot a channel between
  1 and 11.
- **Automatic channel selection (`PROBOT_WIFI_AUTO_CHANNEL 1`) is OFF by
  default and is not recommended for a fleet.** Each robot scans the band
  independently; when robots power on at the same time, none is transmitting
  yet, so each sees an empty band and **all of them may land on the same
  channel (channel 1)**: it piles them up instead of spreading them. It only
  makes sense when there is a single robot in the environment (home or
  workshop). The selected channel is shown on Serial and on the Logs page.
- Phone hotspots and spectator devices also fill 2.4 GHz; do not let anyone
  start a hotspot around the robots during a match.
- To debug signal problems in the field, the `/health` endpoint reports
  RSSI; if it is worse than -70 dBm, look at distance or antenna problems.

## Servo usage (fixing jitter)

Servo jitter has two common causes, and both are outside the library:

1. **Timer conflict:** if `analogWrite` (motors, ~1 kHz) and a servo (50 Hz)
   land on the same LEDC timer, one corrupts the other's frequency. probot
   provides no servo class (you drive the hardware); to prevent jitter, give
   the servo a **high LEDC channel**. Motor `analogWrite` uses channels from
   the bottom (0, 1, 2...), so they do not collide:
   ```cpp
   #define SERVO_PIN 4
   void teleopInit() { ledcAttachChannel(SERVO_PIN, 50, 14, 7); } // 50 Hz, 14-bit, channel 7
   void teleopLoop() {
     uint16_t us = 500 + (angle/180.0f)*2000;        // 0-180 deg -> 500-2500 us
     ledcWrite(SERVO_PIN, (uint32_t)us * 16383 / 20000);
   }
   void teleopStop() { ledcWrite(SERVO_PIN, 0); }    // cut the pulse
   ```
   Full example: `examples/ServoTest`.
2. **Power:** do not power the servo from the ESP32's 5V/3V3 pin. Momentary
   WiFi current draws sag the voltage and the servo twitches. Give the servo
   a **separate 5-6 V supply (BEC/UBEC)** and join the grounds.

If you use a PCA9685, the PWM frequency for servo outputs must be **50 Hz**
(at 1 kHz a servo pulse width physically cannot be produced).

## Status LED and RSL

The built-in NeoPixel shows **only the match state**, and the library drives
its color. There is no API to set a color by hand, because the LED color
always carries a meaning.

| Color | Meaning |
|---|---|
| Solid blue | DS not connected |
| Blinking blue | DS connected + STOPPED |
| Solid yellow | INIT (Auto or TeleOp), waiting for Start |
| Blinking yellow | TRANSITION: Auto finished, TeleOp preselected; waiting for Init |
| Blinking orange | AUTO_RUN |
| Blinking green | TELEOP_RUN |
| Blinking red | Stalled: a loop took longer than 2 s, being held safe |
| Solid red | Emergency stop (latched, reboot required) |

**RSL (robot signal light):** if you `#define PROBOT_RSL_PIN <gpio>`, the
library blinks that digital pin only in AUTO_RUN/TELEOP_RUN, and keeps it
**solid on** in INIT, STOPPED, TRANSITION and E-stop.

## Battery measurement

There are three ways to feed the battery gauge in the UI (if none is
enabled, the gauge says "Veri yok", meaning no data):

**1) Voltage divider + ADC:** the cheapest option, two resistors.

```
BAT+ ──[100k]──┬──[22k]── GND
               │
             GPIO5 (ADC1) ── 100nF ── GND
```

```cpp
#define PROBOT_BATTERY_ADC_PIN  5     // MUST be GPIO1-10 (ADC1)
#define PROBOT_BATTERY_R_TOP_K  100
#define PROBOT_BATTERY_R_BOT_K  22
```

- The pin must be chosen from **GPIO1-10**: ADC2 pins (GPIO11-20) do not
  work while WiFi is on. A wrong pin is caught at compile time with
  [PB-E104].
- 100k/22k brings the 12.6 V peak of a 3S LiPo down to 2.27 V (the linear
  region of the ADC). Put a 100nF capacitor on the midpoint, and connect the
  divider **after the main switch** so it does not drain the battery
  (~0.1 mA) while the robot is off.
- The reading is eFuse-calibrated and uses an 8-sample average plus
  smoothing, giving roughly 1-2% accuracy; if you see a difference against a
  multimeter, fine-tune with `PROBOT_BATTERY_TRIM`.

**2) INA219 / INA226 I2C sensor:** more precise, no soldering.

```cpp
#define PROBOT_BATTERY_INA       226   // or 219
// optional: PROBOT_BATTERY_INA_ADDR / _SDA / _SCL
```

If the sensor cannot be reached, a [PB-E306] warning is raised at runtime
and the gauge returns to "Veri yok". Instantaneous current is also read from
the INA shunt: `probot::io::battery::currentAmps()`.

> **Do not use the INA's I2C bus from user code.** The library reads the INA
> from its own task loop; if you attach a second device (IMU, OLED...) from
> your robot code to the same `Wire` bus, readings can get mixed up. If you
> have your own I2C device, either choose the ADC method for the battery or
> move your device to `Wire1` (separate pins).
>
> **Shunt note:** current is computed with `PROBOT_BATTERY_INA_SHUNT_MOHM`
> (default 100). The INA226's +/-81.92 mV shunt range saturates at +/-0.82 A
> with 100 mohm, so define the shunt value of your module (for example
> 2 mohm). Voltage measurement does not depend on the shunt.

**3) Manual feed:** if you have your own measurement:
`probot::setBatteryVoltage(v)`.

The gauge (Dashboard) shows the average of roughly the last 8 seconds; the
**Logs → History** chart plots samples without averaging, so watch voltage
sag under motor load there.

## Connection behavior (safety)

- If joystick data stops for **500 ms**, axes and buttons are zeroed
  automatically, so motors do not run away on the last command.
- If the DS stays completely silent for **10 s**, the robot goes to STOP
  (it can be put in soft mode with `PROBOT_DS_TIMEOUT_FORCE_STOP 0`).
- Only **one client** can control at a time (the first IP to connect becomes
  the owner). A second device opening the UI gets `403`. `/health` and
  `/info` do not require ownership, so referee and monitoring devices can
  read them freely.

## FTC OpMode lifecycle

- The model is the same as the FTC OpMode flow: **select Autonomous / TeleOp
  → INIT → START → STOP**. The mode can only be changed in STOPPED or
  TRANSITION.
- On INIT the matching `init()` is called once, during RUN `loop()` is
  called continuously, and on every exit from INIT or RUN the matching
  `stop()` is called once.
- The Auto timer starts after `autonomousStart()`. When it ends,
  `autonomousStop()` runs, TeleOp is preselected, and the system waits in
  TRANSITION for a new INIT; TeleOp does not start automatically.
- All hooks run in **one persistent task**, only at loop boundaries.
  A stop or phase change does not **interrupt** user code midway, so a
  Wire/I2C or malloc lock is never orphaned (the root cause of the freezes
  in older versions).
- **Rule:** every `teleopLoop`/`autonomousLoop` pass must eventually
  **return** (recommendation: under about 2 s). Blocking is allowed,
  *infinite* blocking is not. Put timeouts on I2C/sensor calls, for example
  `Wire.setTimeOut(50);` after `Wire.begin()`, otherwise a stuck device locks
  the pass.
- **Stop is cooperative:** when the current pass returns, the active
  OpMode's `stop()` runs (at most one loop period of delay). For an
  immediate cut, use the emergency stop.
- **Stall (halt-safe):** if a pass does not return within
  `PROBOT_LOOP_DEADLINE_MS` (2 s), input is zeroed, the LED turns red and
  the robot is held safe. **The task is not killed and the chip is not
  rebooted** (homing and relative state are preserved).

### Advanced: initLoop and start

`autonomousInitLoop()` / `teleopInitLoop()` are called repeatedly between
INIT and START while the robot stays still; input is neutral and the RSL
stays solid. They are for reading field randomization with a camera,
publishing gyro/sensor calibration status, or running pre-match checks. The
stall deadline applies here too.

`autonomousStart()` / `teleopStart()` are for one-time timestamps and state
resets at the moment of START. On most robots the first pass of the loop is
enough, which is why the main examples do not use these advanced hooks.

## Emergency stop

- The red **EMERGENCY STOP** button in the UI (or
  `/robotControl?cmd=estop`) kills the user task, runs the active OpMode's
  `stop()` (if in INIT/RUN) in a fresh task under a watchdog, and **latches
  the robot until reboot** (there is no hook in STOPPED/TRANSITION; Init and
  Start are rejected; the "Reboot" button or a power cycle clears it). It
  stops even a frozen loop.
- For a real safety guarantee, put a **hardware E-stop** on the
  power/enable line: it is the only layer that works even if the chip locks
  up completely. If you wire the library's `PROBOT_ESTOP_ENABLE_PIN` to the
  motor drivers' enable line, emergency stop also cuts that line in
  hardware.

## Writing code with AI

When you ask Gemini / ChatGPT / Claude to write robot code, add these lines
at the start of your prompt:

```text
Write Arduino code for ESP32 with the "probot" library (0.4.0).
First read the API reference:
https://raw.githubusercontent.com/probot-studio/probot-core/stable/API.md
Rules:
- Do NOT define setup()/loop(). Use the autonomousInit/Loop/Stop and
  teleopInit/Loop/Stop triples; two loops and two stops are required.
- Joystick: auto js = probot::io::joystick_api::makeDefault();
  js.getLeftY() etc. (-1..+1). Methods such as getLeftX do NOT exist on
  probot::io::gamepad().
- For servos use raw LEDC: ledcAttachChannel(pin,50,14,7) in the relevant
  init (high channel, so it does not collide with motor analogWrite), and
  ledcWrite in teleopLoop.
- teleopLoop is called at ~50 Hz; do not write infinite loops or long
  blocking calls in it.
```

Machine-readable summary: [`llms.txt`](llms.txt) · Full reference: [`API.md`](API.md)

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Sketch too big" | Partition Scheme → Huge APP (3MB No OTA) |
| `#error ... [PB-E101/E02]` | Write the macros before `#include <probot.h>`: [details](https://probotstudio.com/docs/hatalar/#pb-e101) |
| UI does not open / 403 | Another device is connected (single-client rule). Close it and wait ~5 s |
| Joystick not visible | Press any button on the gamepad (browsers only detect a gamepad after a button press) |
| No gamepad | In the Joystick tab, manually enable the **Keyboard** (on a computer) or **Touch** (on a phone) source; it is not selected automatically |
| Frequent disconnects | Channel conflict: give robots **different fixed channels** (defaults: 1/6/11) |
| `undefined reference to teleopLoop` | A mandatory hook is missing [PB-E201]: define all four (they may be empty) |
| Blinking red LED, robot unresponsive | Deadline miss [PB-E301]: the loop did not return for 2 s; [details](https://probotstudio.com/docs/hatalar/#pb-e301) |
| Servo jitters | See "Servo usage" above |

## Development and testing

Prerequisites: `arduino-cli` (or Arduino IDE 2.x), the Arduino-ESP32 core,
and the Adafruit NeoPixel library (`make libs`).

```bash
make list                                  # list the examples
make build EXAMPLE=JoystickTest            # build one example (or EXAMPLE=all)
make upload EXAMPLE=JoystickTest PORT=/dev/ttyACM0
make serial                                # serial monitor, 115200 baud
make tests/control_tests && ./tests/control_tests   # host unit tests, no hardware
```

`make build` first runs `tools/sync_version.py`, which propagates the
`VERSION` file into the metadata files and doc headers. See
[CLAUDE.md](CLAUDE.md) for the full engineering guide (architecture, release
process, rules for changes).

## Contributing

Contributions are welcome. Open pull requests against the `dev` branch;
`stable` is the release branch. Read [CONTRIBUTING.md](CONTRIBUTING.md) and
[CLAUDE.md](CLAUDE.md), and follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Support and license

- Bug reports: https://github.com/probot-studio/probot-core/issues
- Docs: [probotstudio.com/docs](https://probotstudio.com/docs)
- License: MIT + Commons Clause ([LICENSE](LICENSE) ·
  [LICENSE-commercial](LICENSE-commercial)). Educational and competition use
  is free; for a commercial license, contact tunagul54@gmail.com.
