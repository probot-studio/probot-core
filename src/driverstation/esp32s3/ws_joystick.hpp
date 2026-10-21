#pragma once
#ifdef ESP32
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <probot/io/gamepad.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/robot/state.hpp>

namespace probot::driverstation::esp32 {

  /**
   * WebSocket handler for /joystick (attaches to existing ESP-IDF httpd)
   *
   * Client -> robot binary frames (first byte = type):
   *   'J' 0x4A  joystick data:
   *     [1]       uint8   axisCount   (max 20)
   *     [2]       uint8   buttonCount (max 20)
   *     [3]       uint8   reserved
   *     [4..]     int16[] axes (big-endian, value = float * 32767)
   *     [4+nA*2]  uint8[] buttons (packed bits, LSB first)
   *   'P' 0x50  idle keepalive (no payload) — sent when no gamepad is
   *             active so the owner slot / DS activity stay alive.
   *
   * Robot -> client binary frames (sent by the DS push task via
   * sendToAll; first byte = type):
   *   'S' 0x53  state+health JSON (also serves as the heartbeat)
   *   'T' 0x54  telemetry buffer text
   */
  class WsJoystick {
  public:
    using OwnerAuthorizer = bool (*)(void* ctx, httpd_req_t* req);
    using PeerFilter      = bool (*)(void* ctx, int sockfd);

    explicit WsJoystick(io::GamepadService& gs) : _gs(gs) {}

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
    // received the frame.
    int sendToAll(const uint8_t* data, size_t len) {
      if (!_server || len == 0) return 0;
      httpd_ws_frame_t frame = {};
      frame.type    = HTTPD_WS_TYPE_BINARY;
      frame.payload = const_cast<uint8_t*>(data);
      frame.len     = len;

      size_t fds = MAX_CLIENTS;
      int clients[MAX_CLIENTS];
      if (httpd_get_client_list(_server, &fds, clients) != ESP_OK) return 0;

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
    static constexpr uint8_t  MAGIC    = 0x4A;   // 'J' joystick frame (client->robot)
    static constexpr uint32_t MAX_AXES = 20;
    static constexpr uint32_t MAX_BTNS = 20;
    static constexpr size_t   MAX_FRAME = 4 + MAX_AXES * 2 + (MAX_BTNS + 7) / 8;

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
      // cleanly instead of silently desyncing.
      if (frame.len == 0) return ESP_OK;
      if (frame.len > MAX_FRAME) return ESP_FAIL;

      uint8_t buf[MAX_FRAME];
      frame.payload = buf;
      ret = httpd_ws_recv_frame(req, &frame, frame.len);
      if (ret != ESP_OK) return ret;

      self->parseFrame(buf, frame.len);
      return ESP_OK;
    }

    void parseFrame(const uint8_t* data, size_t len) {
      if (len < 4 || data[0] != MAGIC) return;

      uint32_t nA = data[1];
      uint32_t nB = data[2];
      // Counts beyond the protocol limit must REJECT, not clamp: the
      // sender laid out buttons after nA*2 axis bytes, so clamping nA
      // would read axis bytes as button bits (phantom presses).
      if (nA > MAX_AXES || nB > MAX_BTNS) return;

      size_t expected = 4 + nA * 2 + (nB + 7) / 8;
      if (len < expected) return;

      float axes[MAX_AXES];
      const uint8_t* p = data + 4;
      for (uint32_t i = 0; i < nA; i++) {
        int16_t raw = static_cast<int16_t>((p[0] << 8) | p[1]);
        axes[i] = static_cast<float>(raw) / 32767.0f;
        p += 2;
      }

      bool buttons[MAX_BTNS];
      for (uint32_t i = 0; i < nB; i++) {
        buttons[i] = (p[i / 8] >> (i % 8)) & 1;
      }

      uint32_t now = millis();
      probot::io::rawGamepad().write(now, axes, nA, buttons, nB);
      if (probot::core::inputsEnabled(probot::robot::state().read())) _gs.write(now, axes, nA, buttons, nB);
      else _gs.write(now, nullptr, 0, nullptr, 0); // INIT/disabled stays neutral
      __atomic_store_n(&probot::robot::g_ds_last_activity_ms, now, __ATOMIC_SEQ_CST);
      probot::robot::noteDsPacket();
    }

    uint8_t* trackFd(int fd) {
      for (auto& s : _sendState) if (s.fd == fd) return &s.fails;
      for (auto& s : _sendState) if (s.fd == -1) { s.fd = fd; s.fails = 0; return &s.fails; }
      return nullptr;
    }

    void clearFd(int fd) {
      for (auto& s : _sendState) if (s.fd == fd) { s.fd = -1; s.fails = 0; }
    }

    io::GamepadService& _gs;
    httpd_handle_t      _server = nullptr;
    SemaphoreHandle_t   _sendMutex = nullptr;
    OwnerAuthorizer     _ownerAuthorizer = nullptr;
    void*               _ownerCtx = nullptr;
    PeerFilter          _peerFilter = nullptr;
    void*               _peerCtx = nullptr;
    SendState           _sendState[MAX_CLIENTS] = {};
  };

}
#endif // ESP32
