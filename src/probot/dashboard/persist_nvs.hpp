#pragma once
#include <Arduino.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <probot/core/core_config.hpp>
#include <probot/core/lifecycle.hpp>
#include <probot/dashboard/dashboard.hpp>
#include <probot/robot/state.hpp>
#include <probot/telemetry/console.hpp>

// NVS storage of PERSISTENT dashboard topics (ESP32 only). The runtime
// installs the backend at setup; the task that stores values starts on the
// first PERSISTENT topic, so sketches without one pay nothing.
namespace probot::dashboard::detail::nvs_store {

  constexpr const char* kNamespace = "pb_dash";
  constexpr uint32_t kTaskStack = 5120;
  constexpr uint32_t kPeriodMs = 250;

  inline long load(const char* key, uint8_t* buf, size_t cap) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return 0;                       // nothing stored yet
    if (err != ESP_OK) return -1;
    size_t n = cap;
    err = nvs_get_blob(h, key, buf, &n);
    nvs_close(h);
    if (err == ESP_OK) return (long)n;
    // Longer than any record this firmware stores: not ours to restore.
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH) return 0;
    return -1;
  }

  inline bool save(const char* key, const uint8_t* data, size_t len) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
  }

  inline bool erase(const char* key) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) return false;
    err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
  }

  inline bool eraseAll() {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
  }

  inline constexpr PersistBackend kBackend{ load, save, erase, eraseAll };

  inline bool g_started = false;     // sysloop only

  inline void reportFailure(const char* path, PersistFailure why) {
    switch (why) {
      case PersistFailure::Read:
        probot::console::error("[PB-E308] dashboard: could not read the stored value of %s; it is kept until the"
                               " Driver Station writes the topic - docs: probotstudio.com/docs/hatalar/#pb-e308", path);
        break;
      case PersistFailure::Erase:
        probot::console::error("[PB-E308] dashboard: could not erase the stored value of %s"
                               " - docs: probotstudio.com/docs/hatalar/#pb-e308", path);
        break;
      case PersistFailure::Store:
        probot::console::error("[PB-E308] dashboard: could not store %s (NVS full, flash error or value over"
                               " 2048 bytes) - docs: probotstudio.com/docs/hatalar/#pb-e308", path);
        break;
    }
  }

  // Stopped or e-stopped. Unlike file transfers, storing goes on after an
  // e-stop (tuning done before it is kept): NVS has its own lock, and if
  // the killed user task left it taken only this task waits.
  inline bool robotIdle() {
    if (__atomic_load_n(&probot::robot::g_estop_latched, __ATOMIC_SEQ_CST)) return true;
    auto s = probot::robot::state().read();
    return core::canAcceptModeChange(s.phase, s.status);
  }

  inline void task(void*) {
    const size_t cap = recordCapacity(maxValue());
    uint8_t* rec = (uint8_t*)malloc(cap);
    uint8_t* cur = (uint8_t*)malloc(cap);
    if (!rec || !cur) {
      probot::console::error("[PB-E308] dashboard: no memory to store PERSISTENT values until restart (%u bytes)"
                             " - docs: probotstudio.com/docs/hatalar/#pb-e308", (unsigned)(2 * cap));
      free(rec); free(cur);
      vTaskDelete(nullptr);
      return;
    }
    for (;;) {
      persistTick((uint32_t)millis(), robotIdle(), rec, cur, cap, reportFailure);
      vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
    }
  }

  // Sysloop (core 0): starts the task once a PERSISTENT topic exists.
  inline void startIfNeeded() {
    if (g_started || !persistUsed()) return;
    g_started = true;
    if (xTaskCreatePinnedToCore(task, "dash_nvs", kTaskStack, nullptr, PRIO_USER, nullptr, CORE_UI) != pdPASS) {
      probot::console::error("[PB-E308] dashboard: no memory to store PERSISTENT values until restart (task)"
                             " - docs: probotstudio.com/docs/hatalar/#pb-e308");
    }
  }

} // namespace probot::dashboard::detail::nvs_store
