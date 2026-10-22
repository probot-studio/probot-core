#pragma once
#ifdef ESP32
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <probot/io/gamepad.hpp>
#include <probot/io/joystick_frame.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/robot/state.hpp>
#include <probot/dashboard/dashboard.hpp>
#include <probot/telemetry/console.hpp>

namespace probot::driverstation::esp32 {

  /**
   * WebSocket handler for /joystick (attaches to existing ESP-IDF httpd)
   *
   * Client -> robot binary frames (first byte = type):
   *   'J' 0x4A  joystick data:
   *     [1]       uint8   axisCount   (max 20)
   *     [2]       uint8   buttonCount (max 20)
   *     [3]       uint8   port (0-5; older pages send 0)
   *     [4..]     int16[] axes (big-endian, value = float * 32767)
   *     [4+nA*2]  uint8[] buttons (packed bits, LSB first)
   *     optional: uint8 valueCount (<= buttonCount), then uint8[] analog
   *               values (0-255) of the first valueCount buttons
   *   'N' 0x4E  gamepad info: [1] u8 port, [2] u8 flags (bit 0: W3C
   *             standard mapping), [3] u8 name length (<= 63), [4..] name
   *   'P' 0x50  idle keepalive (no payload) — sent when no gamepad is
   *             active so the owner slot / DS activity stay alive.
   *   'W' 0x57  dashboard write: [1..2] u16 id (LE), [3] u8 value type,
   *             [4..] value bytes
   *             (up to PROBOT_DASH_MAX_VALUE). Only queued here; robot
   *             code applies it with dashboard::drainWrites().
   *   'Q' 0x51  resync: re-send every dashboard schema, announce and value,
   *             and every console line still held.
   *
   * Robot -> client binary frames (sent by the DS push task via
   * sendToAll; first byte = type):
   *   'S' 0x53  state+health JSON (also serves as the heartbeat)
   *   'T' 0x54  telemetry buffer text
   *   'L' 0x4C  console lines (see probot/telemetry/console.hpp)
   *   'C' 'A' 'V' 'R' 'K'  dashboard schema, announce, values, remove,
   *             write refusal (see probot/dashboard/dashboard.hpp)
   */
  class WsJoystick {
  public:
    using OwnerAuthorizer = bool (*)(void* ctx, httpd_req_t* req);
    using PeerFilter      = bool (*)(void* ctx, int sockfd);

    WsJoystick() = default;

    // Optional gatekeeper invoked at WS handshake and for every incoming
    // frame. Returning false rejects the client (non-owner).
    void setOwnerAuthorizer(OwnerAuthorizer authorizer, void* ctx) {
      _ownerAuthorizer = authorizer;
      _ownerCtx = ctx;
    }

    // Broadcast-time gate: newer IDF versions stop invoking the URI
    // handler at WS handshake time, so handshake-only enforcement can
    // silently disappear on a core update. The filter re-checks each
    // receiving fd; non-owner sockets are closed instead of served.
    void setPeerFilter(PeerFilter filter, void* ctx) {
      _peerFilter = filter;
      _peerCtx = ctx;
    }

    void attach(httpd_handle_t server) {
      _server = server;
      _sendMutex = xSemaphoreCreateMutex();
      // Dashboard writes exceed MAX_FRAME. httpd runs every handler on one
      // task, so a single receive buffer serves all sessions.
      _bigCap = 4 + probot::dashboard::detail::maxValue();
      _big = (uint8_t*)malloc(_bigCap);
      if (!_big) _bigCap = 0;

      httpd_uri_t ws_uri = {
        .uri      = "/joystick",
        .method   = HTTP_GET,
        .handler  = wsHandler,
        .user_ctx = this,
        .is_websocket             = true,
        .handle_ws_control_frames = true,
      };
      httpd_register_uri_handler(_server, &ws_uri);

      Serial.println("[WS   ] WebSocket handler attached to /joystick");
    }

    // Broadcast one binary frame to every connected WS client. Sends are
    // serialized behind _sendMutex (concurrent sends to one fd corrupt
    // frames — esp-idf #14495); per-fd consecutive failures close the
    // session after SEND_MAX_FAILS. Returns the number of clients that
    // received the frame; *targets (optional) gets how many it tried to
    // reach.
    int sendToAll(const uint8_t* data, size_t len, int* targets = nullptr) {
      if (targets) *targets = 0;
      if (!_server || len == 0) return 0;
      httpd_ws_frame_t frame = {};
      frame.type    = HTTPD_WS_TYPE_BINARY;
      frame.payload = const_cast<uint8_t*>(data);
      frame.len     = len;

      size_t fds = MAX_CLIENTS;
      int clients[MAX_CLIENTS];
      if (httpd_get_client_list(_server, &fds, clients) != ESP_OK) return 0;
      if (targets) {
        for (size_t i = 0; i < fds; i++)
          if (httpd_ws_get_fd_info(_server, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET) ++*targets;
      }

      if (_sendMutex && xSemaphoreTake(_sendMutex, pdMS_TO_TICKS(500)) != pdTRUE) return 0;

      // Prune tracked fds that are no longer WS clients
      for (auto& s : _sendState) {
        if (s.fd == -1) continue;
        if (httpd_ws_get_fd_info(_server, s.fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
          s.fd = -1; s.fails = 0;
        }
      }

      int sent = 0;
      for (size_t i = 0; i < fds; i++) {
        if (httpd_ws_get_fd_info(_server, clients[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
        if (_peerFilter && !_peerFilter(_peerCtx, clients[i])) {
          httpd_sess_trigger_close(_server, clients[i]);
          clearFd(clients[i]);
          if (targets) --*targets;
          continue;
        }
        uint8_t* fails = trackFd(clients[i]);
        esp_err_t err = httpd_ws_send_frame_async(_server, clients[i], &frame);
        if (err != ESP_OK) {
          if (fails && ++(*fails) >= SEND_MAX_FAILS) {
            Serial.printf("[WS   ] /joystick send fail x%u, closing fd=%d\n",
                          (unsigned)*fails, clients[i]);
            httpd_sess_trigger_close(_server, clients[i]);
            clearFd(clients[i]);
          }
        } else {
          sent++;
          if (fails) *fails = 0;
        }
      }
      if (_sendMutex) xSemaphoreGive(_sendMutex);
      return sent;
    }

    void closeAll() {
      if (!_server) return;
      size_t fds = MAX_CLIENTS;
      int clients[MAX_CLIENTS];
      if (httpd_get_client_list(_server, &fds, clients) != ESP_OK) return;
      for (size_t i = 0; i < fds; i++) {
        if (httpd_ws_get_fd_info(_server, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
          httpd_sess_trigger_close(_server, clients[i]);
        }
      }
    }

  private:
    static constexpr uint8_t  MAGIC    = io::frame::kJoystick;
    static constexpr uint8_t  INFO     = io::frame::kInfo;
    static constexpr size_t   MAX_FRAME = io::frame::kMaxSize;

    // Close a WS session only after this many consecutive send
    // failures. Tolerates brief RF hiccups in noisy environments
    // (a single lost frame used to close the connection).
    static constexpr uint8_t  SEND_MAX_FAILS = 3;
    // Must cover EVERY httpd session (plain HTTP keep-alive sockets
    // included): httpd_get_client_list fails outright when the array is
    // too small, which would silently stop all broadcasts. Ceiling is
    // CONFIG_LWIP_MAX_SOCKETS(16) - 3 reserved.
    static constexpr size_t   MAX_CLIENTS    = 13;

    struct SendState { int fd = -1; uint8_t fails = 0; };

    static esp_err_t wsHandler(httpd_req_t* req) {
      auto* self = static_cast<WsJoystick*>(req->user_ctx);

      if (req->method == HTTP_GET) {
        // WS handshake — reject non-owner clients so a stray tablet
        // can't hijack joystick input.
        if (self->_ownerAuthorizer && !self->_ownerAuthorizer(self->_ownerCtx, req)) {
          return ESP_FAIL;
        }
        return ESP_OK;
      }

      // Frame-time owner re-check (owner may have changed since handshake).
      if (self->_ownerAuthorizer && !self->_ownerAuthorizer(self->_ownerCtx, req)) {
        int fd = httpd_req_to_sockfd(req);
        if (fd >= 0 && self->_server) {
          httpd_sess_trigger_close(self->_server, fd);
        }
        return ESP_OK;
      }

      // Step 1: 0-length receive to learn frame size & type
      httpd_ws_frame_t frame = {};
      frame.payload = nullptr;
      esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
      if (ret != ESP_OK) return ret;

      // Handle control frames. With handle_ws_control_frames=true WE
      // own payload consumption — leaving payload bytes unread desyncs
      // the whole TCP stream for the session.
      if (frame.type == HTTPD_WS_TYPE_CLOSE) return ESP_OK;
      if (frame.type == HTTPD_WS_TYPE_PING) {
        // RFC 6455 5.5.3: the pong must echo the ping payload (<=125B).
        uint8_t pbuf[128] = {0};
        if (frame.len > 0) {
          if (frame.len > sizeof(pbuf)) return ESP_FAIL;
          frame.payload = pbuf;
          ret = httpd_ws_recv_frame(req, &frame, frame.len);
          if (ret != ESP_OK) return ret;
        }
        httpd_ws_frame_t pong = {};
        pong.type    = HTTPD_WS_TYPE_PONG;
        pong.payload = pbuf;
        pong.len     = frame.len;
        // Serialize against the push task: concurrent WS sends to one
        // fd interleave header/payload and corrupt frames (esp-idf
        // #14495).
        esp_err_t perr = ESP_OK;
        if (!self->_sendMutex || xSemaphoreTake(self->_sendMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          perr = httpd_ws_send_frame(req, &pong);
          if (self->_sendMutex) xSemaphoreGive(self->_sendMutex);
        }
        return perr;
      }
      if (frame.type == HTTPD_WS_TYPE_PONG) {
        // Drain any payload so the stream stays aligned.
        if (frame.len > 0 && frame.len <= MAX_FRAME) {
          uint8_t scratch[MAX_FRAME];
          frame.payload = scratch;
          httpd_ws_recv_frame(req, &frame, frame.len);
        } else if (frame.len > MAX_FRAME) {
          return ESP_FAIL; // closes the misbehaving session
        }
        return ESP_OK;
      }
      if (frame.type != HTTPD_WS_TYPE_BINARY) return ESP_OK;

      // Step 2: bounds check, then receive payload. An oversize frame
      // cannot be skipped without draining it — close the session
      // cleanly instead of silently desyncing. Only a dashboard write may
      // exceed MAX_FRAME; the type byte is checked once it is read.
      if (frame.len == 0) return ESP_OK;
      uint8_t small[MAX_FRAME];
      uint8_t* buf = small;
      if (frame.len > MAX_FRAME) {
        if (frame.len > self->_bigCap) return ESP_FAIL;
        buf = self->_big;
      }
      frame.payload = buf;
      ret = httpd_ws_recv_frame(req, &frame, frame.len);
      if (ret != ESP_OK) return ret;

      switch (buf[0]) {
        case MAGIC:
          if (frame.len <= MAX_FRAME) self->parseFrame(buf, frame.len);
          break;
        case INFO:
          if (frame.len <= MAX_FRAME) self->parseInfo(buf, frame.len);
          break;
        case probot::dashboard::detail::FRAME_WRITE:
          if (frame.len >= 4) {
            probot::dashboard::detail::pushWrite((uint16_t)(buf[1] | (buf[2] << 8)), buf[3],
                                                 buf + 4, frame.len - 4);
          }
          break;
        case probot::dashboard::detail::FRAME_RESYNC:
          probot::dashboard::detail::requestResync();
          probot::console::detail::requestResend();
          break;
        default:
          break;
      }
      return ESP_OK;
    }

    void parseFrame(const uint8_t* data, size_t len) {
      io::frame::Joystick f;
      if (!io::frame::decodeJoystick(data, len, f)) return;
      uint32_t now = millis();
      // INIT/disabled keeps the gated input neutral.
      io::detail::applyFrame(f.port, now, core::inputsEnabled(robot::state().read()),
                             f.axes, f.axisCount, f.buttons, f.buttonCount, f.values, f.valueCount);
      __atomic_store_n(&probot::robot::g_ds_last_activity_ms, now, __ATOMIC_SEQ_CST);
      probot::robot::noteDsPacket();
    }

    void parseInfo(const uint8_t* data, size_t len) {
      io::frame::Info f;
      if (io::frame::decodeInfo(data, len, f)) io::detail::setInfo(f.port, f.name, f.nameLen, f.standard);
    }

    uint8_t* trackFd(int fd) {
      for (auto& s : _sendState) if (s.fd == fd) return &s.fails;
      for (auto& s : _sendState) if (s.fd == -1) { s.fd = fd; s.fails = 0; return &s.fails; }
      return nullptr;
    }

    void clearFd(int fd) {
      for (auto& s : _sendState) if (s.fd == fd) { s.fd = -1; s.fails = 0; }
    }

    httpd_handle_t      _server = nullptr;
    SemaphoreHandle_t   _sendMutex = nullptr;
    OwnerAuthorizer     _ownerAuthorizer = nullptr;
    void*               _ownerCtx = nullptr;
    PeerFilter          _peerFilter = nullptr;
    void*               _peerCtx = nullptr;
    SendState           _sendState[MAX_CLIENTS] = {};
    uint8_t*            _big = nullptr;
    size_t              _bigCap = 0;
  };

}
#endif // ESP32
