#pragma once
#ifdef ESP32
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <Arduino.h>
#include <Preferences.h>
#include <errno.h>
#include <DNSServer.h>
#include <probot/robot/match_info.hpp>
#include <probot/robot/state.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/io/battery.hpp>
#include <probot/io/gamepad.hpp>
#include <probot/telemetry/telemetry.hpp>
#include <probot/telemetry/console.hpp>
#include <probot/dashboard/dashboard.hpp>
#include "index_html_gz.h"

// Dashboard push cadence and per-tick byte budget (see pushDashboard()).
#ifndef PROBOT_DASH_PERIOD_MS
#define PROBOT_DASH_PERIOD_MS 50
#endif
#ifndef PROBOT_DASH_MAX_BYTES_PER_TICK
#define PROBOT_DASH_MAX_BYTES_PER_TICK 8192
#endif
static_assert(PROBOT_DASH_PERIOD_MS >= 10 && PROBOT_DASH_PERIOD_MS <= 250,
              "[PB-E108] PROBOT_DASH_PERIOD_MS must be 10-250. Docs: probotstudio.com/docs/hatalar/#pb-e108");
#include "ws_joystick.hpp"
#include "ds_files.hpp"

#ifndef PROBOT_WIFI_AP_PASSWORD
#error "[PB-E101] Driver station AP password not provided. Define PROBOT_WIFI_AP_PASSWORD (>=8 chars) before including probot.h. Docs: probotstudio.com/docs/hatalar/#pb-e101"
#endif
static_assert(sizeof(PROBOT_WIFI_AP_PASSWORD) - 1 >= 8, "[PB-E101] PROBOT_WIFI_AP_PASSWORD must be at least 8 characters. Docs: probotstudio.com/docs/hatalar/#pb-e101");

#ifndef PROBOT_WIFI_AP_SSID
  #define PROBOT_WIFI_AP_SSID "Probot"
  #ifndef PROBOT_WIFI_AP_SSID_MAC_SUFFIX
    #define PROBOT_WIFI_AP_SSID_MAC_SUFFIX
  #endif
  #warning "PROBOT_WIFI_AP_SSID not defined. Using auto-generated SSID (Probot-XXXXXX). Define a custom SSID for better identification."
#endif
static_assert(sizeof(PROBOT_WIFI_AP_SSID) - 1 >= 1, "[PB-E103] PROBOT_WIFI_AP_SSID must be at least 1 character. Docs: probotstudio.com/docs/hatalar/#pb-e103");
#ifdef PROBOT_WIFI_AP_SSID_MAC_SUFFIX
static_assert(sizeof(PROBOT_WIFI_AP_SSID) - 1 <= 25, "[PB-E103] PROBOT_WIFI_AP_SSID must be 25 characters or fewer when MAC suffix is enabled. Docs: probotstudio.com/docs/hatalar/#pb-e103");
#else
static_assert(sizeof(PROBOT_WIFI_AP_SSID) - 1 <= 32, "[PB-E103] PROBOT_WIFI_AP_SSID must be 32 characters or fewer. Docs: probotstudio.com/docs/hatalar/#pb-e103");
#endif

#ifndef PROBOT_WIFI_AP_CHANNEL
#error "[PB-E102] WiFi AP channel not provided. Define PROBOT_WIFI_AP_CHANNEL (1-13) before including probot.h. Docs: probotstudio.com/docs/hatalar/#pb-e102"
#endif

// Boot-time auto channel select is OPT-IN and OFF by default. A robot scans
// the band and picks its own channel ONLY when PROBOT_WIFI_AUTO_CHANNEL is 1.
// For a fleet (a competition) leave this off and assign each robot a fixed,
// distinct channel by hand — robots booting together all see an empty band
// and would converge on the same channel. See README "Yarışma günü".
#ifndef PROBOT_WIFI_AUTO_CHANNEL
#define PROBOT_WIFI_AUTO_CHANNEL 0
#endif

#if PROBOT_WIFI_AUTO_CHANNEL
static_assert(PROBOT_WIFI_AP_CHANNEL >= 0 && PROBOT_WIFI_AP_CHANNEL <= 13,
              "[PB-E102] PROBOT_WIFI_AP_CHANNEL must be 0-13 (used as the fallback when the auto-select scan finds nothing). Docs: probotstudio.com/docs/hatalar/#pb-e102");
#else
static_assert(PROBOT_WIFI_AP_CHANNEL >= 1 && PROBOT_WIFI_AP_CHANNEL <= 13,
              "[PB-E102] PROBOT_WIFI_AP_CHANNEL must be 1-13. To auto-pick the channel at boot, set PROBOT_WIFI_AUTO_CHANNEL 1 (single-robot use only). Docs: probotstudio.com/docs/hatalar/#pb-e102");
#endif

// How long the owner slot survives without any request from the owning
// client before another client may take over.
#ifndef PROBOT_DS_OWNER_TIMEOUT_MS
#define PROBOT_DS_OWNER_TIMEOUT_MS 5000
#endif

// 802.11b rates: disabled by default. With b-rates on, beacons go out at
// 1 Mbps DSSS (~2.5 ms airtime each — ~2.5% of the channel per AP); with
// them off, 6 Mbps OFDM (~0.4%). Every WiFi-certified 2.4 GHz client
// since 802.11g (any 2010+ phone/tablet) supports OFDM. Set to 1 only if
// you must support a pre-802.11g device.
#ifndef PROBOT_WIFI_ENABLE_11B
#define PROBOT_WIFI_ENABLE_11B 0
#endif

// Protected Management Frames (802.11w). Set to 1 to require PMF, which
// blocks deauth-spoofing attacks (a real sabotage vector at events) but
// also blocks rare clients without PMF support. Default off for maximum
// compatibility with old tablets.
#ifndef PROBOT_WIFI_PMF_REQUIRED
#define PROBOT_WIFI_PMF_REQUIRED 0
#endif

// Captive portal: answer every DNS query with the robot's IP and spoof
// the OS connectivity probes so joining the AP pops a landing page —
// students never type an IP. Set to 0 to disable.
#ifndef PROBOT_CAPTIVE_PORTAL
#define PROBOT_CAPTIVE_PORTAL 1
#endif

namespace probot::driverstation::esp32::diag {
  // Written from the WiFi event task, read by the push task / handlers.
  inline volatile uint8_t  g_last_disc_reason = 0;
  inline volatile uint32_t g_last_disc_ms     = 0;
  inline volatile int32_t  g_sta_count        = 0;
  // Captive-portal redirect target; the 404 error handler has no
  // user_ctx, so this lives at namespace scope.
  inline char g_portal_url[48] = "http://192.168.4.1/portal";
}

namespace probot::driverstation::esp32 {
  class DriverStation {
  public:
    explicit DriverStation(robot::StateService& rs) : _rs(rs) {}

    void begin(){
      const char* pw = PROBOT_WIFI_AP_PASSWORD;
      String ssid = String(PROBOT_WIFI_AP_SSID);
#ifdef PROBOT_WIFI_AP_SSID_MAC_SUFFIX
      char suffix[8];
      snprintf(suffix, sizeof(suffix), "-%06X", (unsigned int)(ESP.getEfuseMac() & 0xFFFFFF));
      ssid += suffix;
#endif
      ap_ssid_ = ssid;

      // Log STA joins/leaves with the IEEE reason code — the field
      // answer to "why did it disconnect?".
      WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        diag::g_sta_count = diag::g_sta_count + 1;
        Serial.printf("[DS   ] STA joined: %02X:%02X:%02X:%02X:%02X:%02X (aid %d)\n",
                      info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                      info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                      info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5],
                      info.wifi_ap_staconnected.aid);
      }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
      WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        if (diag::g_sta_count > 0) diag::g_sta_count = diag::g_sta_count - 1;
        diag::g_last_disc_reason = info.wifi_ap_stadisconnected.reason;
        diag::g_last_disc_ms = millis();
        Serial.printf("[DS   ] STA left: %02X:%02X:%02X:%02X:%02X:%02X reason=%d\n",
                      info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                      info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                      info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5],
                      (int)info.wifi_ap_stadisconnected.reason);
      }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

      wifi_country_t country = { .cc = "TR", .schan = 1, .nchan = 13, .policy = WIFI_COUNTRY_POLICY_MANUAL };

      // Channel resolution. The default is the fixed macro channel. A manual
      // pin saved from the UI (NVS 1-13) overrides everything. Auto-select
      // runs ONLY when compiled in (PROBOT_WIFI_AUTO_CHANNEL) and not pinned;
      // a saved 0 just means "clear the pin, use the firmware default".
      bool autoMode = (PROBOT_WIFI_AUTO_CHANNEL != 0);
      channel_   = PROBOT_WIFI_AP_CHANNEL;
      ch_source_ = autoMode ? "auto" : "macro";
      {
        Preferences prefs;
        if (prefs.begin("probot", /*readOnly=*/true)) {
          int nvsCh = prefs.getInt("ch", -1);
          prefs.end();
          if (nvsCh >= 1 && nvsCh <= 13) {
            autoMode   = false;          // a manual pin wins over auto-select
            channel_   = nvsCh;
            ch_source_ = "nvs";
          }
          // nvsCh == 0 → "use firmware default": leave autoMode/channel_ as is.
        }
      }

      if (autoMode) {
        // Auto-select: scan the band and pick the least congested of the
        // non-overlapping channels. Adds ~2-3 s to boot. Clients find the
        // AP by SSID regardless of channel, so this is transparent to the
        // driver station. OFF by default — see PROBOT_WIFI_AUTO_CHANNEL.
        WiFi.mode(WIFI_STA);
        esp_wifi_set_country(&country);
        channel_ = autoSelectChannel();
        ch_source_ = "auto";
      }

      WiFi.mode(WIFI_AP);
#if !PROBOT_WIFI_ENABLE_11B
      // esp_wifi_config_11b_rate is documented to run between init and
      // start; WiFi.mode() already started the driver, so bounce it
      // once at boot (no clients yet — harmless).
      esp_wifi_stop();
      esp_wifi_set_country(&country);
      esp_err_t rate_err = esp_wifi_config_11b_rate(WIFI_IF_AP, true);
      if (rate_err != ESP_OK) {
        Serial.printf("[DS   ] 11b rate disable failed: 0x%x\n", rate_err);
      }
      esp_wifi_start();
#else
      esp_wifi_set_country(&country);
#endif
      WiFi.softAP(ssid.c_str(), pw, channel_);
      esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
      esp_wifi_set_ps(WIFI_PS_NONE);
      // The TX power API quantizes downward: requesting 19.5 dBm (=78)
      // actually yields 18 dBm; any request >= 80 yields the true API
      // max of 20 dBm. WIFI_POWER_21dBm therefore buys +2 dB over the
      // old WIFI_POWER_19_5dBm setting.
      WiFi.setTxPower(WIFI_POWER_21dBm);
#if PROBOT_WIFI_PMF_REQUIRED
      {
        wifi_config_t apcfg;
        if (esp_wifi_get_config(WIFI_IF_AP, &apcfg) == ESP_OK) {
          apcfg.ap.pmf_cfg.required = true;
          esp_wifi_set_config(WIFI_IF_AP, &apcfg);
        }
      }
#endif

      Serial.println("[DS   ] ========================================");
      Serial.print("[DS   ] WiFi SSID: ");
      Serial.println(ssid);
      Serial.print("[DS   ] Password:  ");
      Serial.println("********");
      Serial.printf("[DS   ] Channel:   %d (%s)\n", channel_, ch_source_);
      Serial.print("[DS   ] IP Address: ");
      Serial.println(WiFi.softAPIP());
      Serial.println("[DS   ] ========================================");

      // Start ESP-IDF httpd (single server, port 80)
      httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
      cfg.server_port    = 80;
      cfg.ctrl_port      = 32768;
      cfg.stack_size     = 8192;
      cfg.max_uri_handlers = 28;   // 24 routes with the captive portal and /ws
      cfg.lru_purge_enable = true;
      // Keep all networking on core 0 with the WiFi stack; core 1 stays
      // exclusively for user teleop/autonomous loops.
      cfg.core_id        = 0;
      // Ceiling is CONFIG_LWIP_MAX_SOCKETS(16) - 3 reserved = 13.
      cfg.max_open_sockets = 10;
      // httpd does NOT set TCP_NODELAY by default; without it, LWIP's
      // Nagle + the client's delayed ACK can hold small server->client
      // frames for tens of ms.
      cfg.open_fn = onSocketOpen;
      // Bound how long a send/recv to a stalled client can block (default 5s
      // each). Symmetric 2 s caps the worker hold for half-open clients.
      cfg.send_wait_timeout = 2;
      cfg.recv_wait_timeout = 2;
      // TCP keepalive detects a vanished client (tablet walked away,
      // battery died) in ~7s at the TCP layer, closing the session
      // without app-level machinery.
      cfg.keep_alive_enable   = true;
      cfg.keep_alive_idle     = 3;
      cfg.keep_alive_interval = 2;
      cfg.keep_alive_count    = 2;

      if (httpd_start(&_server, &cfg) != ESP_OK) {
        Serial.println("[DS   ] Failed to start HTTP server");
        return;
      }

      // Register HTTP routes
      registerUri("/",                HTTP_GET,  handleRoot);
      registerUri("/updateController", HTTP_POST, handleUpdateController);
      registerUri("/robotControl",     HTTP_GET,  handleRobotControl);
      registerUri("/setChannel",       HTTP_GET,  handleSetChannel);
      registerUri("/getState",         HTTP_GET,  handleGetState);
      registerUri("/getBattery",       HTTP_GET,  handleGetBattery);
      registerUri("/telemetry",        HTTP_GET,  handleTelemetry);
      registerUri("/health",           HTTP_GET,  handleHealth);
      registerUri("/info",             HTTP_GET,  handleInfo);
      registerUri("/opmodes",          HTTP_GET,  handleOpModes);
      registerUri("/match",            HTTP_GET,  handleMatchGet);
      registerUri("/match",            HTTP_POST, handleMatchSet);
      registerUri("/files",            HTTP_GET,  handleFiles);
      registerUri("/files/get",        HTTP_GET,  handleFileGet);

#if PROBOT_CAPTIVE_PORTAL
      // Captive portal: spoofed OS connectivity probes + catch-all DNS
      // make the landing page pop when a device joins the AP.
      snprintf(diag::g_portal_url, sizeof(diag::g_portal_url), "http://%s/portal",
               WiFi.softAPIP().toString().c_str());
      registerUri("/portal",              HTTP_GET, handlePortal);
      registerUri("/generate_204",        HTTP_GET, handleProbeRedirect);  // Android
      registerUri("/gen_204",             HTTP_GET, handleProbeRedirect);  // Android (alt)
      registerUri("/hotspot-detect.html", HTTP_GET, handleProbeRedirect);  // iOS/macOS
      registerUri("/connecttest.txt",     HTTP_GET, handleProbeRedirect);  // Windows
      registerUri("/ncsi.txt",            HTTP_GET, handleProbeRedirect);  // Windows legacy
      registerUri("/redirect",            HTTP_GET, handleProbeRedirect);
      registerUri("/canonical.html",      HTTP_GET, handleProbeRedirect);  // Firefox
      registerUri("/wpad.dat",            HTTP_GET, handleWpad);           // stop proxy-probe storms
      httpd_register_err_handler(_server, HTTPD_404_NOT_FOUND, handle404Redirect);

      _dns_started = _dns.start(53, "*", WiFi.softAPIP());
      Serial.printf("[DS   ] Captive portal %s\n", _dns_started ? "active" : "DNS FAILED");
#endif

      // Attach WebSocket handler (with owner gatekeeper). The peer
      // filter additionally re-checks every broadcast recipient — the
      // handshake-time check stops being invoked on newer IDF releases.
      _ws.setOwnerAuthorizer(&DriverStation::wsOwnerAuthorizer, this);
      _ws.setPeerFilter(&DriverStation::wsPeerFilter, this);
      _ws.attach(_server);

      // Push task: streams state/health/telemetry to WS clients so the
      // page never has to poll over HTTP. Core 0 with the rest of
      // networking; user code keeps core 1.
      xTaskCreatePinnedToCore(pushTaskEntry, "ds_push", 4096, this, 3, &_push_task, 0);

      Serial.println("[DS   ] HTTP server started on port 80");
    }

    // Called from sysloop (~1 kHz): answers pending captive-portal DNS
    // queries. Non-blocking.
    void processDns() {
#if PROBOT_CAPTIVE_PORTAL
      if (_dns_started) _dns.processNextRequest();
#endif
    }

    void expireOwnerIfIdle(){
      if (_owner_timeout_ms == 0) return;
      uint32_t now = millis();
      char prev[sizeof(_owner_str)] = {0};
      uint32_t idle = 0;
      bool expired = false;
      portENTER_CRITICAL(&_owner_mux);
      if (_owner_set && (uint32_t)(now - _owner_last_ms) > _owner_timeout_ms){
        idle = now - _owner_last_ms;
        strncpy(prev, _owner_str, sizeof(prev) - 1);
        releaseOwnerLocked();
        expired = true;
      }
      portEXIT_CRITICAL(&_owner_mux);
      if (expired){
        Serial.printf("[DS   ] Owner expired: %s idle %lu ms\n",
                      prev, (unsigned long)idle);
        onOwnerReleased(now);
      }
    }

    void forceDisconnect(uint32_t now_ms){
      Serial.println("[DS   ] Force disconnect: connection timeout");
      char prev[sizeof(_owner_str)] = {0};
      bool released = false;
      portENTER_CRITICAL(&_owner_mux);
      if (_owner_set){
        strncpy(prev, _owner_str, sizeof(prev) - 1);
        releaseOwnerLocked();
        released = true;
      }
      portEXIT_CRITICAL(&_owner_mux);
      if (released){
        Serial.printf("[DS   ] Owner released: %s\n", prev);
        onOwnerReleased(now_ms);
      }
      _ws.closeAll();
      Serial.println("[DS   ] WS connections closed");
    }

  private:
    // ── Helpers ──

    // Pick the least congested of the three classic non-overlapping
    // 2.4 GHz channels (1/6/11). Each visible network adds interference
    // weight to channels within ±3 of its own (20 MHz overlap), stronger
    // signals weigh more. Ties go to the lower channel.
    static int autoSelectChannel() {
      static constexpr int CANDIDATES[] = {1, 6, 11};
      constexpr int NCAND = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);
      Serial.println("[DS   ] Scanning band for channel auto-select...");
      int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
      int32_t score[NCAND] = {};
      for (int i = 0; i < n; i++) {
        int ch = WiFi.channel(i);
        int32_t rssi = WiFi.RSSI(i);
        // -100 dBm (negligible) .. -30 dBm (very strong) → 5..70
        int32_t strength = rssi + 100;
        if (strength < 5)  strength = 5;
        if (strength > 70) strength = 70;
        for (int c = 0; c < NCAND; c++) {
          int d = ch - CANDIDATES[c];
          if (d < 0) d = -d;
          if (d < 4) score[c] += (4 - d) * strength;
        }
      }
      int best = 0;
      for (int c = 1; c < NCAND; c++) {
        if (score[c] < score[best]) best = c;
      }
      Serial.printf("[DS   ] Scan: %d networks. Scores ch1=%ld ch6=%ld ch11=%ld -> ch%d\n",
                    n, (long)score[0], (long)score[1], (long)score[2],
                    CANDIDATES[best]);
      WiFi.scanDelete();
      return CANDIDATES[best];
    }

    void registerUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
      httpd_uri_t u = {
        .uri      = uri,
        .method   = method,
        .handler  = handler,
        .user_ctx = this,
      };
      httpd_register_uri_handler(_server, &u);
    }

    static DriverStation* self(httpd_req_t* req) {
      return static_cast<DriverStation*>(req->user_ctx);
    }

    static esp_err_t onSocketOpen(httpd_handle_t hd, int sockfd) {
      int yes = 1;
      setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
      return ESP_OK;
    }

    static int8_t readApRssi() {
      wifi_sta_list_t sta_list;
      if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0) {
        return sta_list.sta[0].rssi;
      }
      return -100;
    }

    static uint32_t computeAutoRemainingMs(const robot::StateSnapshot& s, uint32_t now_ms) {
      if (s.phase != probot::robot::Phase::AUTO_RUN ||
          s.autoStartMs == 0 || s.autoPeriodSeconds <= 0) {
        return 0;
      }
      uint32_t total_ms = static_cast<uint32_t>(s.autoPeriodSeconds) * 1000u;
      uint32_t elapsed = now_ms - s.autoStartMs;
      return (elapsed >= total_ms) ? 0u : (total_ms - elapsed);
    }

    static const char* opModeName(robot::OpMode mode) {
      return robot::OpModeRegistry::modeName(mode);
    }

    static unsigned batteryDecivolts(const robot::StateSnapshot& s) {
      // unset / NaN -> 0 ("no data" in the UI); tek yuvarlama mantığı
      // battery.hpp'de yaşar.
      return probot::io::battery::detail::toDecivolts(s.batteryVoltage);
    }

    // ── WS push task ──
    // Streams 'S' (state+health JSON, also the heartbeat) and 'T'
    // (telemetry text) frames so the page never polls over HTTP.

    size_t buildStateHealthJson(char* out, size_t out_size, const robot::StateSnapshot& s, uint32_t now) {
      uint32_t joyLast = io::detail::newestFrameMs();
      long joyAge = -1;
      if (joyLast) {
        int32_t d = (int32_t)(now - joyLast);
        joyAge = d < 0 ? 0 : d;   // a frame can land between the two reads
      }
      // Battery as fixed-point decivolts — float printf support varies by
      // libc build, and a dropped %f would corrupt the whole JSON frame.
      unsigned battDv = batteryDecivolts(s);
      int n = snprintf(out, out_size,
        "{\"status\":%u,\"phase\":%u,\"selectedMode\":\"%s\",\"autoPeriodSeconds\":%d,"
        "\"autoRemainingMs\":%u,\"batt\":%u.%u,\"rssi\":%d,\"up\":%lu,\"heap\":%lu,\"dm\":%s,"
        "\"estop\":%s,\"joyAgeMs\":%ld,\"sta\":%ld,\"disc\":%u,\"dashMax\":%lu,"
        "\"opMode\":\"%llu\",\"omv\":%lu,\"util\":%s,\"loopMs\":%lu,\"overruns\":%lu,\"msq\":%lu}",
        static_cast<unsigned>(s.status),
        static_cast<unsigned>(s.phase),
        opModeName(s.selectedMode),
        (int)s.autoPeriodSeconds,
        (unsigned)computeAutoRemainingMs(s, now),
        battDv / 10, battDv % 10,
        (int)readApRssi(),
        (unsigned long)now,
        (unsigned long)ESP.getFreeHeap(),
        s.deadlineMiss ? "true" : "false",
        __atomic_load_n(&probot::robot::g_estop_latched, __ATOMIC_SEQ_CST) ? "true" : "false",
        joyAge,
        (long)diag::g_sta_count,
        (unsigned)diag::g_last_disc_reason,
        (unsigned long)probot::dashboard::detail::maxValue(),
        (unsigned long long)s.opModeId,
        (unsigned long)robot::opModes().version(),
        ds::utilityAvailable() ? "true" : "false",
        (unsigned long)probot::loopPeriodMs(),
        (unsigned long)probot::loopOverruns(),
        (unsigned long)robot::matchStore().seq());
      if (n < 0) return 0;
      return ((size_t)n >= out_size) ? out_size - 1 : (size_t)n;
    }

    static void pushTaskEntry(void* arg) {
      static_cast<DriverStation*>(arg)->pushTaskLoop();
    }

    void pushTaskLoop() {
      uint32_t lastStateSent = 0;
      uint32_t lastStateSeq = ~0u;
      uint32_t lastTelemSeq = ~0u;
      uint32_t lastSlowTick = 0;
      for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PROBOT_DASH_PERIOD_MS));
        if (!_server) continue;
        uint32_t now = millis();
        if ((uint32_t)(now - lastSlowTick) >= 250) {
          lastSlowTick = now;
          pushStateAndTelemetry(now, lastStateSent, lastStateSeq, lastTelemSeq);
        }
        // After 'S'/'T', so dashboard traffic never delays the heartbeat.
        pushDashboard(now);
        pushConsole();
      }
    }

    void pushStateAndTelemetry(uint32_t now, uint32_t& lastStateSent,
                               uint32_t& lastStateSeq, uint32_t& lastTelemSeq) {
      // State changes go out on the next tick; otherwise re-sent every
      // second so the frame doubles as the link heartbeat.
      auto s = _rs.read();
      if (s.seq != lastStateSeq || (uint32_t)(now - lastStateSent) >= 1000) {
        char buf[448];
        buf[0] = 'S';
        size_t n = 1 + buildStateHealthJson(buf + 1, sizeof(buf) - 1, s, now);
        _ws.sendToAll(reinterpret_cast<const uint8_t*>(buf), n);
        lastStateSent = now;
        lastStateSeq = s.seq;
      }

      uint32_t tseq = probot::telemetry::getSeq();
      if (tseq != lastTelemSeq) {
        char tbuf[1 + probot::telemetry::detail::BUFFER_SIZE + 1];
        tbuf[0] = 'T';
        size_t n = probot::telemetry::copyBuffer(tbuf + 1, sizeof(tbuf) - 1);
        _ws.sendToAll(reinterpret_cast<const uint8_t*>(tbuf), 1 + n);
        lastTelemSeq = tseq;
      }
    }

    // Dashboard frames, at most PROBOT_DASH_MAX_BYTES_PER_TICK per tick so
    // a burst of values cannot delay the 'S' heartbeat. The frame buffer is
    // allocated the first time anything is published.
    void pushDashboard(uint32_t now) {
      if (!_dashBuf) {
        if (!probot::dashboard::detail::active()) return;
        _dashCap = probot::dashboard::detail::frameCapacity();
        _dashBuf = (uint8_t*)malloc(_dashCap);
        if (!_dashBuf) return;
      }
      size_t sent = 0;
      while (sent < PROBOT_DASH_MAX_BYTES_PER_TICK) {
        size_t n = probot::dashboard::detail::buildFrame(_dashBuf, _dashCap, now);
        if (n == 0) break;
        int targets = 0;
        int reached = _ws.sendToAll(_dashBuf, n, &targets);
        // Announces, removes, schemas and refusals are sent once. If a
        // client missed one, start over rather than leave it with ids it
        // does not know (a missed 'V' only delays that value).
        if (reached < targets && _dashBuf[0] != probot::dashboard::detail::FRAME_VALUES)
          probot::dashboard::detail::requestResync();
        sent += n;
      }
    }

    // Console lines, up to kConsoleFramesPerTick frames per tick so a
    // replay after 'Q' catches up fast. Clients drop lines they already have
    // (by sequence), so a frame some client missed is simply sent again on
    // the next tick.
    static constexpr int kConsoleFramesPerTick = 4;
    void pushConsole() {
      if (probot::console::detail::takeResend()) _conCursor = {};
      for (int i = 0; i < kConsoleFramesPerTick; ++i) {
        const probot::console::detail::Cursor at = _conCursor;
        size_t n = probot::console::detail::buildFrame(_conCursor, _conBuf, sizeof _conBuf);
        if (n == 0) break;
        int targets = 0;
        if (_ws.sendToAll(_conBuf, n, &targets) < targets) { _conCursor = at; break; }
      }
    }

    // ── Client IP extraction ──

    static bool getClientIP(httpd_req_t* req, char* out, size_t out_len) {
      return getPeerIP(httpd_req_to_sockfd(req), out, out_len);
    }

    static bool getPeerIP(int sockfd, char* out, size_t out_len) {
      struct sockaddr_in6 addr;
      socklen_t addr_len = sizeof(addr);
      if (getpeername(sockfd, (struct sockaddr*)&addr, &addr_len) != 0) {
        out[0] = '\0';
        return false;
      }
      // IPv4-mapped IPv6: extract the IPv4 part
      if (addr.sin6_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&addr.sin6_addr)) {
        struct in_addr v4;
        memcpy(&v4, &addr.sin6_addr.s6_addr[12], 4);
        inet_ntop(AF_INET, &v4, out, out_len);
      } else if (addr.sin6_family == AF_INET) {
        inet_ntop(AF_INET, &((struct sockaddr_in*)&addr)->sin_addr, out, out_len);
      } else {
        inet_ntop(AF_INET6, &addr.sin6_addr, out, out_len);
      }
      return true;
    }

    // ── Owner enforcement ──
    // Owner fields are touched from two tasks (httpd handlers here, the
    // sysloop expiry/timeout path above), so every access goes through
    // _owner_mux. Critical sections stay short: no logging or I/O inside.

    bool enforceOwner(httpd_req_t* req, bool sendHttpError = true) {
      uint32_t now = millis();
      char ip[48];
      if (!getClientIP(req, ip, sizeof(ip))) {
        if (sendHttpError) {
          httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot determine client IP");
        }
        return false;
      }

      enum class Verdict : uint8_t { ACQUIRED, REFRESHED, REJECTED };
      Verdict verdict;
      char prev[sizeof(_owner_str)] = {0};
      uint32_t idle = 0;
      bool expired = false;

      portENTER_CRITICAL(&_owner_mux);
      if (_owner_set && _owner_timeout_ms > 0 &&
          (uint32_t)(now - _owner_last_ms) > _owner_timeout_ms) {
        idle = now - _owner_last_ms;
        strncpy(prev, _owner_str, sizeof(prev) - 1);
        releaseOwnerLocked();
        expired = true;
      }
      if (!_owner_set) {
        strncpy(_owner_str, ip, sizeof(_owner_str) - 1);
        _owner_str[sizeof(_owner_str) - 1] = '\0';
        _owner_set = true;
        _owner_last_ms = now;
        verdict = Verdict::ACQUIRED;
      } else if (strcmp(ip, _owner_str) == 0) {
        _owner_last_ms = now;
        verdict = Verdict::REFRESHED;
      } else {
        strncpy(prev, _owner_str, sizeof(prev) - 1);
        verdict = Verdict::REJECTED;
      }
      portEXIT_CRITICAL(&_owner_mux);

      if (expired) {
        Serial.printf("[DS   ] Owner timeout: %s idle %lu ms (limit %lu ms)\n",
                      prev, (unsigned long)idle, (unsigned long)_owner_timeout_ms);
        onOwnerReleased(now);
      }

      if (verdict == Verdict::ACQUIRED) {
        __atomic_store_n(&probot::robot::g_ds_last_activity_ms, now, __ATOMIC_SEQ_CST);
        _rs.setClientCount(now, 1);
        Serial.printf("[DS   ] Owner acquired: %s\n", ip);
        return true;
      }
      if (verdict == Verdict::REFRESHED) {
        __atomic_store_n(&probot::robot::g_ds_last_activity_ms, now, __ATOMIC_SEQ_CST);
        return true;
      }

      Serial.printf("[DS   ] Rejected %s (owner: %s)\n", ip, prev);
      if (sendHttpError) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "Another client is already connected.", HTTPD_RESP_USE_STRLEN);
      }
      return false;
    }

    static bool wsOwnerAuthorizer(void* ctx, httpd_req_t* req) {
      auto* ds = static_cast<DriverStation*>(ctx);
      return ds->enforceOwner(req, /*sendHttpError=*/false);
    }

    // Broadcast-time check used by WsJoystick::sendToAll: only the
    // owner's IP (or anyone, while no owner is set) may receive state/
    // telemetry pushes. Read-only — never acquires the slot.
    static bool wsPeerFilter(void* ctx, int sockfd) {
      auto* ds = static_cast<DriverStation*>(ctx);
      char ip[48];
      if (!getPeerIP(sockfd, ip, sizeof(ip))) return false;
      portENTER_CRITICAL(&ds->_owner_mux);
      bool ok = !ds->_owner_set || (strcmp(ip, ds->_owner_str) == 0);
      portEXIT_CRITICAL(&ds->_owner_mux);
      return ok;
    }

    // Clears the owner slot. Caller must hold _owner_mux.
    void releaseOwnerLocked() {
      _owner_set = false;
      _owner_str[0] = '\0';
    }

    // Post-release side effects — run OUTSIDE the critical section.
    // Zeroes the gamepad so user code reading axes/buttons does not see
    // stale values (last-command runaway when the link dies).
    void onOwnerReleased(uint32_t now_ms) {
      io::detail::neutralizeAll(now_ms, true);
      // Another client may have legitimately taken the slot between the
      // release (inside the mux) and here — don't clobber its count.
      portENTER_CRITICAL(&_owner_mux);
      bool hasOwner = _owner_set;
      portEXIT_CRITICAL(&_owner_mux);
      _rs.setClientCount(now_ms, hasOwner ? 1 : 0);
    }

    // Replace JSON/HTML-breaking characters for safe embedding of the
    // user-defined SSID into /info JSON and the portal page.
    static void sanitizeSsid(const char* in, char* out, size_t out_size) {
      size_t j = 0;
      for (size_t i = 0; in[i] && j + 1 < out_size; i++) {
        char c = in[i];
        if (c == '"' || c == '\\' || c == '<' || c == '>' || c == '&' ||
            (unsigned char)c < 0x20) {
          c = '_';
        }
        out[j++] = c;
      }
      out[j] = '\0';
    }

    // ── Parsers (unchanged) ──

    static bool parseFloatArray(const char* body, const char* key, float* out, uint32_t maxCount, uint32_t& written) {
      written = 0;
      const char* keyPos = strstr(body, key);
      if (!keyPos) return false;
      const char* lb = strchr(keyPos, '[');
      if (!lb) return false;
      const char* rb = strchr(lb, ']');
      if (!rb) return false;
      const char* p = lb + 1;
      while (p < rb && written < maxCount) {
        while (p < rb && (*p==' '||*p=='\n'||*p=='\r'||*p=='\t'||*p==',')) p++;
        if (p >= rb) break;
        char* q = nullptr;
        float v = strtof(p, &q);
        if (q == p) break;
        out[written++] = v;
        p = q;
      }
      return true;
    }

    static bool parseBoolArray(const char* body, const char* key, bool* out, uint32_t maxCount, uint32_t& written) {
      written = 0;
      const char* keyPos = strstr(body, key);
      if (!keyPos) return false;
      const char* lb = strchr(keyPos, '[');
      if (!lb) return false;
      const char* rb = strchr(lb, ']');
      if (!rb) return false;
      const char* p = lb + 1;
      while (p < rb && written < maxCount) {
        while (p < rb && (*p==' '||*p=='\n'||*p=='\r'||*p=='\t'||*p==',')) p++;
        if (p >= rb) break;
        if ((rb - p) >= 4 && strncmp(p, "true", 4) == 0) { out[written++] = true; p += 4; }
        else if ((rb - p) >= 5 && strncmp(p, "false", 5) == 0) { out[written++] = false; p += 5; }
        else break;
      }
      return true;
    }

    // ── Route handlers ──

    static esp_err_t handleRoot(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      // The page is stored gzip-compressed (tools/gen_ui.py); every browser
      // the Driver Station supports decodes it.
      httpd_resp_set_type(req, "text/html");
      httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
      const char* ptr = reinterpret_cast<const char*>(MAIN_page_gz);
      size_t remaining = MAIN_page_gz_len;
      while (remaining > 0) {
        size_t chunk = (remaining > 2048) ? 2048 : remaining;
        if (httpd_resp_send_chunk(req, ptr, chunk) != ESP_OK) {
          httpd_resp_send_chunk(req, NULL, 0);
          return ESP_FAIL;
        }
        ptr += chunk;
        remaining -= chunk;
      }
      httpd_resp_send_chunk(req, NULL, 0);
      return ESP_OK;
    }

    static esp_err_t handleUpdateController(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      char body[512];
      int total = (int)req->content_len;
      if (total <= 0 || total > (int)sizeof(body) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body size");
        return ESP_OK;
      }
      // A body can arrive split across TCP segments — read all of it.
      int got = 0;
      while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) {
          httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body recv failed");
          return ESP_OK;
        }
        got += r;
      }
      body[got] = '\0';

      float axes[20]; bool buttons[20]; uint32_t nA = 0, nB = 0;
      parseFloatArray(body, "axes", axes, 20, nA);
      parseBoolArray(body, "buttons", buttons, 20, nB);
      uint32_t now = millis();
      io::detail::applyFrame(0, now, core::inputsEnabled(ds->_rs.read()), axes, nA, buttons, nB);
      robot::noteDsPacket();

      httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    static esp_err_t handleRobotControl(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      char query[128] = {0};
      httpd_req_get_url_query_str(req, query, sizeof(query));

      char cmd[32] = {0};
      char modeVal[12] = {0};
      char idVal[24] = {0};
      char autoLenVal[8] = {0};
      httpd_query_key_value(query, "cmd", cmd, sizeof(cmd));
      httpd_query_key_value(query, "val", modeVal, sizeof(modeVal));
      // A truncated id (ESP_ERR_HTTPD_RESULT_TRUNC) must not select its prefix.
      bool idOk = httpd_query_key_value(query, "id", idVal, sizeof(idVal)) == ESP_OK;
      httpd_query_key_value(query, "autoLen", autoLenVal, sizeof(autoLenVal));

      int autoLen = atoi(autoLenVal);
      // autoLen*1000 happens in the sysloop — clamp to avoid signed
      // overflow on hostile/typo'd input.
      if (autoLen > 3600) autoLen = 3600;

      // Emergency stop and reboot are handled even while latched.
      if (strcmp(cmd, "estop") == 0) {
        __atomic_store_n(&probot::robot::g_estop_requested, 1u, __ATOMIC_SEQ_CST);
        httpd_resp_send(req, "ESTOP", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
      }
      if (strcmp(cmd, "reboot") == 0) {
        __atomic_store_n(&probot::robot::g_reboot_requested, 1u, __ATOMIC_SEQ_CST);
        httpd_resp_send(req, "REBOOT", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
      }

      // Terminal latch: once emergency-stopped the robot stays dead until a
      // reboot — refuse anything that would re-arm it.
      if (__atomic_load_n(&probot::robot::g_estop_latched, __ATOMIC_SEQ_CST)) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "EMERGENCY STOPPED — reboot required", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
      }

      auto s = ds->_rs.read();
      auto conflict = [req](const char* body) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
      };

      static constexpr const char* kStopBeforeMode =
        "[PB-E409] STOP before changing mode - docs/hatalar#pb-e409";
      uint32_t now = millis();
      if (strcmp(cmd, "mode") == 0) {
        if (!core::canAcceptModeChange(s.phase, s.status)) return conflict(kStopBeforeMode);
        robot::OpMode mode;
        if (strcmp(modeVal, "auto") == 0) mode = robot::OpMode::AUTO;
        else if (strcmp(modeVal, "teleop") == 0) mode = robot::OpMode::TELEOP;
        else if (strcmp(modeVal, "utility") == 0 && ds::utilityAvailable()) mode = robot::OpMode::UTILITY;
        else {
          httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "val must be auto, teleop or an available utility");
          return ESP_OK;
        }
        if (!ds->_rs.setSelectionIfIdle(now, mode, robot::opModes().preferred(mode)))
          return conflict(kStopBeforeMode);
      } else if (strcmp(cmd, "opmode") == 0) {
        if (!core::canAcceptModeChange(s.phase, s.status)) return conflict(kStopBeforeMode);
        char* end = nullptr;
        unsigned long long id = 0;
        if (idOk && idVal[0] >= '0' && idVal[0] <= '9') {
          errno = 0;
          id = strtoull(idVal, &end, 10);
          if (errno == ERANGE) id = 0;
        }
        robot::OpMode mode;
        if (!id || !end || *end || !robot::opModes().find((uint64_t)id, &mode)) {
          httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id is not a listed OpMode");
          return ESP_OK;
        }
        if (!ds->_rs.setSelectionIfIdle(now, mode, (uint64_t)id))
          return conflict(kStopBeforeMode);
        robot::opModes().remember(mode, (uint64_t)id);
      } else if (strcmp(cmd, "init") == 0) {
        // The status write re-checks its rule atomically: probot::requestStop()
        // may change the state from another task meanwhile.
        static constexpr const char* kInitConflict = "[PB-E409] INIT requires STOPPED — docs/hatalar#pb-e409";
        if (!core::canAcceptInit(s.phase, s.status)) return conflict(kInitConflict);
        if (autoLen > 0) ds->_rs.setAutoPeriodSeconds(now, autoLen);
        // The list may have changed during the last run; never INIT an
        // option that is no longer offered.
        ds::detail::reconcileSelection();
        if (!ds->_rs.setStatusIf(now, robot::Status::INIT, core::canAcceptInit)) return conflict(kInitConflict);
        ds->_rs.setDeadlineMiss(now, false);
      } else if (strcmp(cmd, "start") == 0) {
        static constexpr const char* kStartConflict = "[PB-E409] START requires INIT — docs/hatalar#pb-e409";
        if (!core::canAcceptStart(s.phase, s.status)) return conflict(kStartConflict);
        if (autoLen > 0) ds->_rs.setAutoPeriodSeconds(now, autoLen);
        if (!ds->_rs.setStatusIf(now, robot::Status::START, core::canAcceptStart)) return conflict(kStartConflict);
      } else if (strcmp(cmd, "stop") == 0) {
        static constexpr const char* kStopConflict = "[PB-E409] STOP requires INIT or RUN — docs/hatalar#pb-e409";
        if (!ds->_rs.setStatusIf(now, robot::Status::STOP, core::canAcceptStop)) return conflict(kStopConflict);
      } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown command");
        return ESP_OK;
      }

      robot::noteDsPacket();
      httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    static esp_err_t handleSetChannel(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      char query[32] = {0};
      char chVal[8] = {0};
      httpd_req_get_url_query_str(req, query, sizeof(query));
      if (httpd_query_key_value(query, "ch", chVal, sizeof(chVal)) != ESP_OK || chVal[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ch parameter missing");
        return ESP_OK;
      }
      int ch = atoi(chVal);
      if (ch < 0 || ch > 13) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ch must be 0-13 (0 = auto at boot)");
        return ESP_OK;
      }

      bool saved = false;
      {
        Preferences prefs;
        if (prefs.begin("probot", /*readOnly=*/false)) {
          saved = prefs.putInt("ch", ch) > 0;
          prefs.end();
        }
      }

      // 1-13: switch live via CSA — beacons announce the migration and
      // compliant clients follow without disconnecting. 0 = clear the saved
      // pin and fall back to the firmware default (the fixed macro channel,
      // or auto-select if PROBOT_WIFI_AUTO_CHANNEL was compiled in); applied
      // at next boot since a scan would drop clients.
      bool live = false;
      if (ch >= 1 && ch <= 13 && ch != ds->channel_) {
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK) {
          cfg.ap.channel = (uint8_t)ch;
          cfg.ap.csa_count = 3;
          if (esp_wifi_set_config(WIFI_IF_AP, &cfg) == ESP_OK) {
            ds->channel_ = ch;
            ds->ch_source_ = "nvs";
            live = true;
            Serial.printf("[DS   ] Channel switching to %d via CSA\n", ch);
          }
        }
      }

      char buf[96];
      snprintf(buf, sizeof(buf), "{\"ok\":%s,\"ch\":%d,\"live\":%s}",
               saved ? "true" : "false", ch, live ? "true" : "false");
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    static esp_err_t handleGetState(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      auto s = ds->_rs.read();
      unsigned battDv = batteryDecivolts(s);
      char buf[256];
      snprintf(buf, sizeof(buf),
               "{\"status\":%u,\"phase\":%u,\"selectedMode\":\"%s\",\"autoPeriodSeconds\":%d,\"autoRemainingMs\":%u,\"batt\":%u.%u,\"estop\":%s,"
               "\"opMode\":\"%llu\",\"omv\":%lu,\"util\":%s,\"msq\":%lu}",
               static_cast<unsigned>(s.status),
               static_cast<unsigned>(s.phase),
               opModeName(s.selectedMode),
               (int)s.autoPeriodSeconds,
               (unsigned)computeAutoRemainingMs(s, millis()),
               battDv / 10, battDv % 10,
               __atomic_load_n(&probot::robot::g_estop_latched, __ATOMIC_SEQ_CST) ? "true" : "false",
               (unsigned long long)s.opModeId,
               (unsigned long)robot::opModes().version(),
               ds::utilityAvailable() ? "true" : "false",
               (unsigned long)robot::matchStore().seq());

      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    // OpMode options as JSON (see robot::OpModeRegistry), streamed in chunks
    // so the registry lock never covers a large copy. A list replaced
    // mid-response closes the connection; the page refetches when the
    // "omv" counter of the 'S' frame changes.
    static esp_err_t handleOpModes(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      auto& reg = robot::opModes();
      const uint32_t ver = reg.version();
      char chunk[256];
      bool stale = false;
      httpd_resp_set_type(req, "application/json");
      for (size_t off = 0;;) {
        size_t n = reg.readJson(ver, off, chunk, sizeof(chunk), &stale);
        if (stale) return ESP_FAIL;
        if (!n) break;
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) return ESP_FAIL;
        off += n;
      }
      return httpd_resp_send_chunk(req, nullptr, 0);
    }

    static esp_err_t sendMatchInfo(httpd_req_t* req) {
      uint32_t seq = 0;
      robot::MatchInfo info = robot::matchStore().read(&seq);
      char buf[robot::kMatchInfoJsonMax + 1];
      size_t n = robot::matchInfoJson(info, seq, buf, sizeof buf);
      httpd_resp_set_type(req, "application/json");
      return httpd_resp_send(req, buf, (ssize_t)n);
    }

    // Match information (robot::MatchInfo) as JSON; the page refetches when
    // the "msq" counter of the 'S' frame changes.
    static esp_err_t handleMatchGet(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;
      return sendMatchInfo(req);
    }

    // Form body (see robot::applyMatchForm), accepted only while
    // core::canChangeMatchInfo(). Replies with the stored information.
    // Check and store cannot be split by an INIT: httpd runs every handler,
    // /robotControl included, on its one task.
    static esp_err_t handleMatchSet(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      // Every field at its longest, each text byte percent-encoded.
      char body[96 + 2 * 3 * robot::MatchInfo::kMaxText];
      int total = (int)req->content_len;
      if (total < 0 || total > (int)sizeof(body) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body size");
        return ESP_OK;
      }
      int got = 0;
      while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) {
          httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body recv failed");
          return ESP_OK;
        }
        got += r;
      }
      body[got] = '\0';

      auto s = ds->_rs.read();
      if (!core::canChangeMatchInfo(s.phase, s.status,
                                    __atomic_load_n(&probot::robot::g_estop_latched, __ATOMIC_SEQ_CST) != 0)) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "[PB-E409] STOP before changing match info - docs/hatalar#pb-e409",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
      }
      robot::MatchInfo info = robot::matchStore().read();
      if (const char* err = robot::applyMatchForm(body, info)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err);
        return ESP_OK;
      }
      robot::matchStore().set(info);
      return sendMatchInfo(req);
    }

    static esp_err_t handleFiles(httpd_req_t* req) {
      auto* ds = self(req);
      if (files_http::replyIfNoShares(req)) return ESP_OK;
      if (!ds->enforceOwner(req)) return ESP_OK;
      return files_http::list(req, ds->_rs);
    }

    static esp_err_t handleFileGet(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;
      return files_http::download(req, ds->_rs);
    }

    static esp_err_t handleGetBattery(httpd_req_t* req) {
      auto* ds = self(req);
      auto s = ds->_rs.read();
      char buf[16];
      dtostrf(s.batteryVoltage, 0, 1, buf);
      httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    static esp_err_t handleTelemetry(httpd_req_t* req) {
      auto* ds = self(req);
      if (!ds->enforceOwner(req)) return ESP_OK;

      char buf[probot::telemetry::detail::BUFFER_SIZE + 1];
      probot::telemetry::copyBuffer(buf, sizeof(buf));
      httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    // /health and /info are OPEN (no owner enforcement).
    // Monitoring stations (judge/referee) need to observe robot liveness
    // without grabbing the DS ownership slot away from the active driver.
    static esp_err_t handleHealth(httpd_req_t* req) {
      auto* ds = self(req);

      auto s = ds->_rs.read();
      uint32_t now = millis();
      uint32_t joyLast = io::detail::newestFrameMs();
      long joyAge = -1;
      if (joyLast) {
        int32_t d = (int32_t)(now - joyLast);
        joyAge = d < 0 ? 0 : d;
      }
      auto dash = probot::dashboard::stats();
      char buf[256];
      snprintf(buf, sizeof(buf),
        "{\"rssi\":%d,\"up\":%lu,\"heap\":%lu,\"dm\":%s,"
        "\"joyAgeMs\":%ld,\"sta\":%ld,\"disc\":%u,"
        "\"dashTopics\":%u,\"dashBytes\":%lu,\"dashDropped\":%lu,"
        "\"loopMs\":%lu,\"overruns\":%lu}",
        (int)readApRssi(),
        (unsigned long)now,
        (unsigned long)ESP.getFreeHeap(),
        s.deadlineMiss ? "true" : "false",
        joyAge,
        (long)diag::g_sta_count,
        (unsigned)diag::g_last_disc_reason,
        (unsigned)dash.topics,
        (unsigned long)dash.bytesUsed,
        (unsigned long)dash.dropped,
        (unsigned long)probot::loopPeriodMs(),
        (unsigned long)probot::loopOverruns());

      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    // ── Captive portal (all owner-free) ──

    static esp_err_t handleProbeRedirect(httpd_req_t* req) {
      httpd_resp_set_status(req, "302 Found");
      httpd_resp_set_hdr(req, "Location", diag::g_portal_url);
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    }

    static esp_err_t handle404Redirect(httpd_req_t* req, httpd_err_code_t) {
      httpd_resp_set_status(req, "302 Found");
      httpd_resp_set_hdr(req, "Location", diag::g_portal_url);
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    }

    // Plain 404 (NOT via httpd_resp_send_err, which would invoke the
    // redirect above) — stops Windows wpad proxy-probe retry storms.
    static esp_err_t handleWpad(httpd_req_t* req) {
      httpd_resp_set_status(req, "404 Not Found");
      httpd_resp_send(req, NULL, 0);
      return ESP_OK;
    }

    // Landing page shown by the OS sign-in sheet. Must not contain the
    // word "Success" (iOS treats that as "no portal"). The OS mini
    // browser is throttled and killed when dismissed, so the page sends
    // users to a real browser instead of hosting the DS itself.
    static esp_err_t handlePortal(httpd_req_t* req) {
      auto* ds = self(req);
      char ssid[40];
      sanitizeSsid(ds->ap_ssid_.c_str(), ssid, sizeof(ssid));
      char page[1024];
      snprintf(page, sizeof(page),
        "<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Probot</title><style>body{font-family:sans-serif;background:#00204d;"
        "color:#e5e4e2;display:flex;flex-direction:column;align-items:center;"
        "justify-content:center;min-height:90vh;text-align:center;gap:24px;margin:0}"
        "a{background:#28a745;color:#fff;padding:18px 36px;border-radius:14px;"
        "text-decoration:none;font-size:1.3rem;font-weight:700}"
        "p{opacity:.85;max-width:34ch;line-height:1.5}</style></head><body>"
        "<h1>&#129302; %s</h1>"
        "<a href=\"http://%s/\">Driver Station'&#305; A&#231;</a>"
        "<p>Buton bu pencerede a&#231;&#305;l&#305;rsa: pencereyi kapat&#305;p "
        "taray&#305;c&#305;da <b>http://%s</b> adresini a&#231;&#305;n.</p>"
        "</body></html>",
        ssid,
        WiFi.softAPIP().toString().c_str(),
        WiFi.softAPIP().toString().c_str());
      httpd_resp_set_type(req, "text/html; charset=utf-8");
      httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    static esp_err_t handleInfo(httpd_req_t* req) {
      auto* ds = self(req);

      // /info is now open (no owner check) so the password field is
      // omitted — anyone connected already has the password; we don't
      // want other teams' monitoring stations harvesting it.
      char ssid[40];
      sanitizeSsid(ds->ap_ssid_.c_str(), ssid, sizeof(ssid));
      char buf[512];
      snprintf(buf, sizeof(buf),
        "{\"ssid\":\"%s\",\"ch\":%d,\"chSource\":\"%s\",\"ip\":\"%s\","
        "\"chip\":\"%s\",\"cpuMhz\":%lu,\"sdk\":\"%s\","
        "\"totalHeap\":%lu,\"totalFlash\":%lu,"
        "\"sketchSize\":%lu,\"freeSketch\":%lu,\"psram\":%lu}",
        ssid,
        ds->channel_,
        ds->ch_source_,
        WiFi.softAPIP().toString().c_str(),
        ESP.getChipModel(),
        (unsigned long)ESP.getCpuFreqMHz(),
        ESP.getSdkVersion(),
        (unsigned long)ESP.getHeapSize(),
        (unsigned long)ESP.getFlashChipSize(),
        (unsigned long)ESP.getSketchSize(),
        (unsigned long)ESP.getFreeSketchSpace(),
        (unsigned long)ESP.getPsramSize());

      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
      return ESP_OK;
    }

    // ── Members ──
    robot::StateService& _rs;
    WsJoystick           _ws;
    httpd_handle_t       _server = nullptr;
    TaskHandle_t         _push_task = nullptr;
    uint8_t*             _dashBuf = nullptr;
    size_t               _dashCap = 0;
    probot::console::detail::Cursor _conCursor;
    uint8_t              _conBuf[1400];   // one 'L' frame; >= console kMinFrame
    static_assert(sizeof(_conBuf) >= probot::console::detail::kMinFrame, "one console line must fit a frame");
    portMUX_TYPE         _owner_mux = portMUX_INITIALIZER_UNLOCKED;
    bool                 _owner_set = false;
    char                 _owner_str[48] = {0};
    uint32_t             _owner_last_ms = 0;
    uint32_t             _owner_timeout_ms = PROBOT_DS_OWNER_TIMEOUT_MS;
    int                  channel_ = PROBOT_WIFI_AP_CHANNEL;
    const char*          ch_source_ = "macro";
    String               ap_ssid_;
#if PROBOT_CAPTIVE_PORTAL
    DNSServer            _dns;
    bool                 _dns_started = false;
#endif
  };
}
#endif // ESP32
