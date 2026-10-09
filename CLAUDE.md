# CLAUDE.md: probot-core

Canonical guide for agents and engineers working in this repository. Read it in full before
making changes. `AGENTS.md` points here.

## Purpose and scope

`probot-core` is the ESP32-S3 robot control library of Probot Studio, written by Tuna. It is
the communication layer for educational robotics competitions: the robot hosts a WiFi access
point and a browser-based Driver Station (embedded in firmware), joystick input arrives over a
binary WebSocket at 50 Hz, and an FTC-style OpMode lifecycle (select mode, INIT, START, STOP)
runs the user's hooks with failsafes (input zeroing, stall detection, emergency stop).

It is deliberately only the communication and safety layer. Motor, encoder, IMU and servo
drivers are not part of the library (the `Servo` class was removed; see `FUTURE_WORK.md`).

Public repository: the repository is public and consumed by students and teachers. Rules that
follow from that:

- **No internal infrastructure details may be committed**: no server paths, IP addresses or
  hostnames of internal machines, no internals of private repositories, no personal phone
  numbers, no credentials or tokens. This applies to code, docs, examples, commit messages and
  PR text. The public support contacts already in `README.md` / `CONTRIBUTING.md` are the only
  contact details to use.
- Everything merged here is visible to the world immediately. When in doubt, leave it out and
  ask an owner.

## Supported targets

- Official target: **ESP32-S3** (`FQBN esp32:esp32:esp32s3`, flags `-DESP32S3
  -DARDUINO_USB_MODE=1`). Other ESP32 variants are not officially supported.
- Arduino-ESP32 core **3.x** (`idf_component.yml`: `espressif/arduino-esp32 >= 3.0.0`,
  ESP-IDF `>= 5.0`).
- Arduino IDE / `arduino-cli` (library metadata in `library.properties`), PlatformIO
  (`library.json`, `platforms: espressif32`, framework `arduino`), and ESP-IDF with the Arduino
  component (`CMakeLists.txt` + `idf_component.yml`; call `probot::runtime_setup()`).
- Dependency: Adafruit NeoPixel `>= 1.12.0` (status LED).
- Sketches need the **Huge APP (3MB No OTA)** partition scheme; the embedded UI makes the
  default partition too small.
- Host build: the pure-logic parts compile and run on a desktop with `g++ -std=c++17`
  (see Commands).

## Architecture

Header-only library under `src/`. Entry point: `src/probot.h` (declares the user hooks, then
includes the modules). Namespace `probot`.

| Path | Role |
|---|---|
| `src/probot.h` | Public entry. Declares `autonomous*` / `teleop*` hooks (loop and stop required, init/initLoop/start are weak) and includes the modules |
| `src/probot/core/core_config.hpp` | Core and priority constants (core 0 = "UI": WiFi, httpd, sysloop; core 1 = "CTRL": user code) |
| `src/probot/core/lifecycle.hpp` | Pure-logic cooperative OpMode state machine (no FreeRTOS); the part host tests exercise |
| `src/probot/core/runtime.hpp` | FreeRTOS plumbing: persistent user task, sysloop, stall deadline, emergency stop, `runtime_setup()` |
| `src/probot/core/lock.hpp` | `core::Mux`: `portMUX` critical section on ESP32, CAS lock on host |
| `src/probot/core/wdt.hpp` | Task watchdog helpers |
| `src/probot/io/gamepad.hpp`, `joystick_api.hpp`, `joystick_mappings/` | Joystick service (input timeout, neutral on loss) and the user-facing `joystick_api::makeDefault()` |
| `src/probot/io/battery.hpp` | Battery measurement: ADC divider or INA219/INA226, compile-time config checks |
| `src/probot/robot/state.hpp`, `system.hpp` | Shared robot state (`Phase`, `Status`) and system singletons |
| `src/probot/telemetry/telemetry.hpp` | Fixed-size telemetry buffer (`probot::printf` / `println`) |
| `src/probot/devices/leds/builtin.hpp` | Status-only NeoPixel and optional RSL pin |
| `src/driverstation/esp32s3/driver_station_esp32.hpp`, `ws_joystick.hpp` | WiFi AP, httpd, HTTP/WS protocol, captive portal, channel selection |
| `src/driverstation/esp32s3/index_html.h` | The Driver Station web UI as one raw string in PROGMEM |
| `tests/` | Host unit tests (custom harness) with Arduino/FreeRTOS stubs in `tests/stubs/` |
| `examples/` | `JoystickTest`, `TankDrive`, `ServoTest` sketches |
| `tools/sync_version.py` | Propagates `VERSION` into metadata and doc headers |

The lifecycle is cooperative: one persistent user task observes the desired state only at
loop boundaries, so stop and phase changes never interrupt a hook that holds a bus or
allocator lock. Emergency stop is the one exception and is terminal until reboot.

## Public API stability and versioning

The public API is everything a sketch can touch: hook names and semantics, `probot::` functions
(`io::joystick_api`, `io::battery`, `setBatteryVoltage`, `printf`, `emergencyStop`, ...), the
`PROBOT_*` / `USER_LOOP_PERIOD_MS` / `NEOPIXEL_*` configuration macros, the HTTP/WebSocket
protocol (`/robotControl`, `/health`, `/info`, `/getState`, binary frames), the `robot::Phase`
numbering, and the persistent error codes (`PB-Exxx`).

- Semantic Versioning (`CHANGELOG.md` follows Keep a Changelog). While the version is `0.x`,
  breaking changes are allowed but must be marked `Breaking` in the changelog and use a `!`
  Conventional Commit (history shows `feat(core)!:`).
- Never repurpose or renumber a `PB-Exxx` code or a `robot::Phase` value silently: the docs
  site deep-links error codes (`probotstudio.com/docs/hatalar/#pb-eXXX`) and clients depend on
  the wire protocol. A change needs a new version and a migration note.
- **Version sources must stay in sync.** `VERSION` is the single source of truth.
  `tools/sync_version.py` (run by `make build` and `make version-sync`) rewrites
  `library.properties`, `library.json`, `idf_component.yml`, the version in the `API.md` title
  line and the `> Library version:` line of `llms.txt`. Edit `VERSION`, run
  `make version-sync`, and commit all resulting changes together. Do not hand-edit the version
  elsewhere. `CHANGELOG.md` is not generated: add the entry yourself.
- Do not change the `# Probot API Referansı (x.y.z)` title format in `API.md` or the
  `> Library version:` line format in `llms.txt`; `sync_version.py` matches them with regexes
  and fails if they are missing.

## Commands

All from the repository root. Requires `arduino-cli` for builds (`make libs` installs
Adafruit NeoPixel; the ESP32 core must be installed).

| Command | What it does |
|---|---|
| `make list` | List examples |
| `make build EXAMPLE=JoystickTest` | Build one example for `esp32:esp32:esp32s3` with `--warnings all` (also runs `sync_version.py`); `EXAMPLE=all` builds every example |
| `make upload EXAMPLE=JoystickTest PORT=/dev/ttyACM0` | Build and flash (needs hardware) |
| `make serial` | Serial monitor at 115200 baud |
| `make tests/control_tests && ./tests/control_tests` | Host unit tests, no hardware (`g++ -std=c++17 -Wall -Wextra -pedantic` with the test defines and stubs) |
| `make test` | `build` (default example) plus host tests |
| `make version-sync` | Run `tools/sync_version.py` |
| `make clean` | Remove `.build/`, the arduino-cli cache and the test binary |

CI (`.github/workflows/ci.yml`) runs on pushes to `stable`, `dev`, `dev-*` and on PRs to
`stable` and `dev`: it installs arduino-cli 1.3.1 and the esp32 core, installs Adafruit
NeoPixel, then runs `make build EXAMPLE=all`, `make tests/control_tests` and
`./tests/control_tests`. Run the same three commands locally before opening a PR.

Tests use a small custom harness (`tests/test_harness.hpp`: `TEST_CASE(...)`,
`EXPECT_TRUE`...); every `tests/*.cpp` is compiled into one binary. Add new test files under
`tests/`; they are picked up by the Makefile wildcard.

## Workflow and releases

Branches: `dev` is the trunk; `stable` is the release branch and is updated from `dev` by PR.
Nobody commits to `stable` directly.

1. Branch from `dev`: `feat/...`, `fix/...`, `docs/...`, `refactor/...`, `chore/...`,
   `test/...`, `ci/...`.
2. Keep the PR small and focused; update examples and docs when user-facing behavior changes.
3. Open the PR against `dev` with a description of what changed, why, test steps and results.
   Documentation PRs also target `dev`.
4. After CI is green and review is done, it lands on `dev`.

Release (dev to stable):

1. On `dev`: set `VERSION`, run `make version-sync`, move the `[Unreleased]` changelog entries
   under the new version heading with the date, commit (`chore(release): ...`).
2. Hardware validation: the changelog convention is that a version reaches `stable` after
   hardware tests on a real robot (see the 0.4.0 note and `FUTURE_WORK.md`). Host tests and
   CI do not replace this.
3. Open a PR `dev` to `stable`; wait for all checks green; an owner approves and merges.
4. Publishing (tags, Library Manager and PlatformIO registry, ESP-IDF component registry,
   docs site updates) is a production action: it needs explicit owner approval. Prepare the
   change; an owner runs it.

`llms.txt` and the AI prompt in the README link to `raw.githubusercontent.com/.../stable/...`,
so `stable` is what external tools read. Do not break those URLs or file paths.

## Embedded engineering rules

Guidance derived from how the code is actually structured. Follow it unless you have a reason
documented in the PR.

- **Two cores, one user task.** Core 0 (`CORE_UI`) runs WiFi, httpd and the sysloop; core 1
  (`CORE_CTRL`) runs the single persistent user task. Do not add work to the user task's loop
  boundary that can block indefinitely.
- **Shared state uses `core::Mux`.** On ESP32 it is a `portMUX` critical section on purpose
  (a spinlock livelocked tasks of different priorities on the same core, see the comment in
  `lock.hpp`). Keep critical sections tiny: copy in or out, no logging, no allocation, no
  blocking calls inside.
- **Lifecycle changes belong in `lifecycle.hpp` and need a host test.** It is the testable,
  FreeRTOS-free core. Keep FreeRTOS, WiFi and hardware calls out of it so the host build keeps
  working.
- **Keep the user-code contract.** Hooks are invoked only at loop boundaries; a stop never
  interrupts a hook; a pass exceeding `PROBOT_LOOP_DEADLINE_MS` is halt-safe (input zeroed,
  task not killed, no reboot); emergency stop is terminal until reboot. Changes must preserve
  these guarantees or version the change as breaking and update the docs.
- **Fail loudly at build time, softly at runtime.** Invalid configuration is caught with
  `#error` / `static_assert` carrying a `PB-E1xx` code (see the battery and WiFi macro
  checks). Runtime problems degrade with a log line carrying a `PB-E3xx` code.
- **Allocation.** The existing code uses fixed-size buffers for hot paths (telemetry is a
  256-byte buffer, state is plain structs) and Arduino `String` only in setup-time Driver
  Station configuration. Do not introduce heap allocation in per-loop or per-frame paths
  (joystick frames, state push, telemetry), and prefer fixed buffers for new ones.
- **No ISR usage exists in the library.** If you add an interrupt handler, keep it minimal,
  mark it `IRAM_ATTR`, and use `FromISR` FreeRTOS variants; there is no precedent here to
  copy.
- **Time and timeouts.** Use `millis()`-based deadlines (all existing timeouts are macro
  configurable in ms). Any new I2C or sensor access in library code must have a timeout.
- **Flash budget.** The embedded UI (`index_html.h`, about 2200 lines) dominates flash use and
  is why Huge APP is required. Check the sketch size after UI changes.
- **WiFi.** 2.4 GHz channels 1-13 only; channel plan defaults are 1/6/11. Do not make
  automatic channel selection default: it stacks simultaneously booted robots on one channel.
- **Pins.** Battery ADC pins must be ADC1 (GPIO1-10 on the S3); ADC2 does not work with WiFi.
  The library claims the I2C bus for INA battery sensing, so user code must not share it.

## Documentation to update with public changes

When a public header, macro, protocol field or error code changes, update in the same PR:

- `API.md`: full reference (hooks table, macros table, error code table, HTTP/WS section).
  User-facing, written in Turkish today; edit in place, do not translate opportunistically.
- `llms.txt`: the rules block for generated code; English.
- `README.md` (English) and `README.tr.md` (Turkish original): macros table, examples,
  troubleshooting, the AI prompt block. Keep the two consistent for factual content.
- `examples/*`: keep every example compiling (`make build EXAMPLE=all`).
- `keywords.txt`: Arduino IDE syntax highlighting. It still lists names that no longer exist
  (for example `Servo`); fix it when touching related API, but verify each keyword exists.
- `CHANGELOG.md`: entry under `[Unreleased]`, with `Breaking` called out.
- Error codes (`PB-Exxx`): also have a page on the public docs site (`probot-docs`
  repository, "Hatalar" page). A new or changed code needs a matching docs PR.
- `FUTURE_WORK.md`: remove or amend an item when you complete it.

## Gotchas

- `make build` mutates tracked files: `sync_version.py` rewrites metadata and doc headers. Run
  `git status` after a build and keep only intended changes.
- The `make test` target builds the default (first) example, which requires `arduino-cli`;
  the host tests alone do not.
- The host test build defines `PROBOT_CLM_NOLOG`, `PROBOT_SCHED_NOLOG`,
  `PROBOT_LOGGER_NO_SCHED_ATTACH` and `PROBOT_BUILTINLED_EXTERNAL`; the Makefile also passes
  the first two when building the `LoopPeriodStress` example name (not present today).
- `index_html.h` is a hand-maintained single raw string literal (`R"=====(` ... `)=====`).
  Do not introduce that closing delimiter inside the HTML, JS or CSS. Keep it consistent with
  the HTTP/WS protocol in `driver_station_esp32.hpp`.
- The fallback templates inside `sync_version.py` mention an old repository URL; they only
  apply when a metadata file is missing. Do not delete the metadata files.
- `.gitignore` excludes `.build/`, `tests/control_tests`, `connection-test/`, `review/`,
  `tuna_test/` and `.claude/`; local experiments there stay local.
- `CONTRIBUTING.md` is bilingual (Turkish, then English) and still lists a personal WhatsApp
  number; the English `README.md` deliberately omits it. Do not copy personal phone numbers
  into new files; prefer GitHub Issues as the support channel.
- Existing `CHANGELOG.md` entries and some source comments (battery, `probot.h` error notes)
  are Turkish. Write new code comments, log messages and changelog entries in English; do not
  rewrite the old text as part of an unrelated change.

## Probot Studio engineering standards

These rules are shared by every repository in the `probot-studio` GitHub organization. A
repository may add stricter rules below; it never relaxes these.

### Organization map

| Repository | What it is | Trunk | Visibility |
|---|---|---|---|
| `probot-studio` | Platform monorepo: probotstudio.com site, shop, accounts and LMS, release pipeline, ops records | `dev` (`prod` is the live pointer) | private |
| `blocks` | Block coding tool (`/blocks/`), shipped to the platform as a versioned surface artifact | `dev` | private |
| `sim` | Robot simulator (`/sim/`) and its match server, shipped as a versioned surface artifact | `dev` | private |
| `probot-core` | ESP32-S3 robot control library (Arduino / ESP-IDF) | `dev` (`stable` is the release branch) | public |
| `probot-docs` | Public documentation site (MkDocs) | `dev` (`stable` is the release branch) | public |
| `probot-egitim` | Curriculum research and lesson production workspace | `main` | private |

GitHub is the single source of truth. The self-hosted Gitea mirror is a backup only: never push
to it and never treat it as authoritative.

### Language: English, everywhere

Everything an engineer or agent authors is written in English:

- Code: identifiers, comments, docstrings, log and error messages, test names.
- Git: branch names, commit subjects and bodies, tags, release notes.
- GitHub: PR titles and descriptions, review comments, issues, discussions.
- Engineering docs: READMEs, ADRs, runbooks, plans, this file.

The only exception is copy that ships to end users in their locale: UI strings, lesson and
curriculum content, marketing pages and legal texts stay in the product language (Turkish
today). Keep that copy in data or i18n files, not hard-coded in logic.

Legacy Turkish identifiers and file names exist. Do not rename them opportunistically inside an
unrelated change. A rename is its own refactor PR that updates every reference, keeps public
contracts compatible (or versions them), and is recorded in the repository's moved-paths log.
New modules, new public APIs and new files are named in English.

### Engineering bar

Write production-grade code that a staff engineer would approve without comments:

- **Correct first.** Handle the edge cases, failure paths and concurrency you can foresee.
  Validate input at trust boundaries; trust nothing that crosses a process or network edge.
- **Tested.** Every behavior change ships with a test that fails without it. Bug fixes start
  with a reproducing test. A skipped test is not a passing test. Refactors that must not change
  behavior prove it (build-output comparison, golden files, contract tests).
- **Typed boundaries.** Public interfaces, wire formats, storage schemas and cross-repo
  contracts are explicitly typed and versioned. Breaking a contract requires a new version and a
  migration path, never a silent change.
- **Errors are handled, not swallowed.** No empty `catch`. Fail loudly at startup on bad
  configuration; degrade gracefully at runtime with an actionable log line.
- **Small and focused.** One concern per PR. Minimal diff. No speculative abstractions, no dead
  code, no commented-out code, no `TODO` without a linked issue.
- **Readable.** Code reads like the code around it. Names say what, comments say why.
- **Secure by default.** No secrets in the repository, logs or artifacts (`.env*` is ignored;
  templates are `.env.template`). Least privilege for tokens, keys and service users. Pin
  dependencies and actions; review lockfile changes.
- **Operable.** New services and jobs come with health checks, structured logs and a documented
  rollback.
- **Fast and accessible.** UI changes respect performance budgets, keyboard and screen-reader
  access, and are verified at 1440x900 and 390x844.

### Repository layout principles

- Apps never import another app's source. Shared code becomes a package with a declared
  dependency and an explicit `exports` map.
- Runtime coupling between apps happens only through written contracts: URL paths, message
  types, storage keys, HTTP endpoints. Contracts live in a documented file and are covered by a
  contract test.
- A tool that leaves the platform monorepo gets its own repository and ships to the platform as
  a pinned, checksummed release artifact (surface lock + `sha256`), never as a source copy.
- Generated files are either never committed, or committed together with the generator change
  and checked for drift in CI. Each repository says which.
- History is preserved: moves use `git mv`; retired content goes to an archive directory, not
  to the bin.

### Git and review workflow

- Work on short-lived branches cut from the trunk: `feat/<scope>-<topic>`, `fix/...`,
  `refactor/...`, `chore/...`, `docs/...`, `test/...`, `ci/...`.
- Commits follow Conventional Commits, fully in English: `type(scope): imperative summary`,
  then a body that explains why. Agent commits carry a `Co-Authored-By:` trailer.
- Every change lands through a PR into the trunk. Rebase merge only: history stays linear and
  every commit of the PR lands as written, so the branch history is part of the review. Before
  merging, each commit builds and passes the tests on its own and has a Conventional Commits
  subject; fixups are folded in (`git commit --fixup <sha>`, then `git rebase --autosquash`).
  The branch is deleted after merge. Promoting the trunk to a release branch (`stable`, `prod`)
  is a fast-forward or a merge commit, never a squash, so both branches keep the same commits.
- **Never merge while CI is red or still running.** Wait for every required check to finish
  green, including after a rebase.
- No direct pushes, force pushes or history rewrites on shared branches (`dev`, `main`, `prod`,
  `stable`). Coordinate any history rewrite with the owners first.
- PR descriptions state what changed, why, how it was verified (commands and results) and how
  to roll it back.

### Production and ownership

- Owners: Tuna (`@tunapro1234`) and Mami (`@mr-kaynak`).
- Anything that touches production (releases, database migrations, nginx, systemd, cron, DNS,
  cloud accounts, published packages) needs explicit owner approval in the conversation first.
  Agents prepare the change and the runbook; an owner runs or approves it.
- Ask before any irreversible or outward-facing action: deleting data or branches, publishing,
  changing repository settings, creating credentials.

### Content rules

- No em dash (U+2014) in code, docs, commits or product copy. Use " - " or rewrite.
- Do not invent product specs. Kit contents, part lists and measurements come from the source
  data (BOM, catalog) or from an owner.
- Retired brand and product terms must not appear anywhere. The list is kept in the platform
  repository's `CLAUDE.md`.
