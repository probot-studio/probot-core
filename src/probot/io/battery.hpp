#pragma once
// Batarya gerilim ölçümü — iki kaynak, ikisi de opsiyonel (define ile seçilir):
//
//   1) Gerilim bölücü + ADC1 pini:
//        #define PROBOT_BATTERY_ADC_PIN   5     // GPIO1-10 (ADC1) — zorunlu şart
//        #define PROBOT_BATTERY_R_TOP_K   100   // üst direnç, kΩ (batarya tarafı)
//        #define PROBOT_BATTERY_R_BOT_K   22    // alt direnç, kΩ (GND tarafı)
//      Devre: BAT+ ──[R_TOP]──┬──[R_BOT]── GND, orta uç → pin + 100nF → GND.
//      ADC2 pinleri (S3'te GPIO11-20) WiFi açıkken KULLANILAMAZ — bu yüzden
//      pin ADC1'den seçilmek zorundadır (derlemede PB-E104 ile yakalanır).
//
//   2) INA219 / INA226 I2C sensörü:
//        #define PROBOT_BATTERY_INA        226   // 219 ya da 226
//        #define PROBOT_BATTERY_INA_ADDR   0x40  // (ops.) I2C adresi
//        #define PROBOT_BATTERY_INA_SDA    8     // (ops.) verilmezse kart default'u
//        #define PROBOT_BATTERY_INA_SCL    9     // (ops.)
//        #define PROBOT_BATTERY_INA_SHUNT_MOHM 100 // (ops.) akım hesabı için
//
//   Ortak (ops.): #define PROBOT_BATTERY_TRIM 1.00f  // multimetre ince ayarı
//
// Hiçbiri tanımlı değilse özellik kapalıdır; probot::setBatteryVoltage() ile
// elle besleme bugüne kadarki gibi çalışır (arayüz 0.0'da "Veri yok" gösterir).
// İkisi birden tanımlanamaz (PB-E105).
//
// Sampling runs every 250 ms in a dedicated low-priority task on core 0, never
// in the sysloop supervisor: a slow or wedged I2C transaction must not delay
// the stall watchdog, DS timeout or emergency-stop handling. Samples are
// EMA-smoothed and written to state only on a 0.1 V change, so each sample
// does not trigger its own 'S' frame push.
#include <stdint.h>
#include <probot/robot/state.hpp>

// ---- konfigürasyon doğrulama ----
#if defined(PROBOT_BATTERY_ADC_PIN) && defined(PROBOT_BATTERY_INA)
#error "[PB-E105] Tek batarya kaynagi secin: PROBOT_BATTERY_ADC_PIN (bolucu) VEYA PROBOT_BATTERY_INA (I2C sensor), ikisi birden degil. Docs: probotstudio.com/docs/hatalar/#pb-e105"
#endif

#if defined(PROBOT_BATTERY_ADC_PIN)
  #define PROBOT_BATTERY_ENABLED 1
  #define PROBOT_BATTERY_SRC_ADC 1
  #if !defined(PROBOT_BATTERY_R_TOP_K) || !defined(PROBOT_BATTERY_R_BOT_K)
  #error "[PB-E104] Gerilim bolucu direncleri eksik: PROBOT_BATTERY_R_TOP_K ve PROBOT_BATTERY_R_BOT_K (kilo-ohm) tanimlanmali. Docs: probotstudio.com/docs/hatalar/#pb-e104"
  #else
  static_assert((PROBOT_BATTERY_R_TOP_K) > 0 && (PROBOT_BATTERY_R_BOT_K) > 0,
    "[PB-E104] Bolucu direncleri pozitif olmali (kilo-ohm). Docs: probotstudio.com/docs/hatalar/#pb-e104");
  #endif
  static_assert(PROBOT_BATTERY_ADC_PIN >= 1 && PROBOT_BATTERY_ADC_PIN <= 10,
    "[PB-E104] PROBOT_BATTERY_ADC_PIN ADC1 pini olmali (ESP32-S3: GPIO1-10). ADC2 (GPIO11-20) WiFi acikken calismaz. Docs: probotstudio.com/docs/hatalar/#pb-e104");
  // Güvenlik çıkışlarıyla çakışma: pin ADC'ye devredilirse e-stop enable
  // hattı fiziksel kesilemez / RSL ve durum LED'i susar. (Bu makrolar burada
  // yalnız kullanıcı tanımladıysa görünür — default'ları daha sonra gelir.)
  #ifdef PROBOT_ESTOP_ENABLE_PIN
  static_assert((PROBOT_BATTERY_ADC_PIN) != (PROBOT_ESTOP_ENABLE_PIN),
    "[PB-E104] PROBOT_BATTERY_ADC_PIN, PROBOT_ESTOP_ENABLE_PIN ile cakisiyor — acil durdurma hatti ADC'ye devredilemez. Docs: probotstudio.com/docs/hatalar/#pb-e104");
  #endif
  #ifdef PROBOT_RSL_PIN
  static_assert((PROBOT_BATTERY_ADC_PIN) != (PROBOT_RSL_PIN),
    "[PB-E104] PROBOT_BATTERY_ADC_PIN, PROBOT_RSL_PIN ile cakisiyor. Docs: probotstudio.com/docs/hatalar/#pb-e104");
  #endif
  #ifdef NEOPIXEL_PIN
  static_assert((PROBOT_BATTERY_ADC_PIN) != (NEOPIXEL_PIN),
    "[PB-E104] PROBOT_BATTERY_ADC_PIN, NEOPIXEL_PIN ile cakisiyor. Docs: probotstudio.com/docs/hatalar/#pb-e104");
  #endif
#elif defined(PROBOT_BATTERY_INA)
  #define PROBOT_BATTERY_ENABLED 1
  #define PROBOT_BATTERY_SRC_INA 1
  #if PROBOT_BATTERY_INA != 219 && PROBOT_BATTERY_INA != 226
  #error "[PB-E105] PROBOT_BATTERY_INA 219 ya da 226 olmali (desteklenen sensorler: INA219, INA226). Docs: probotstudio.com/docs/hatalar/#pb-e105"
  #endif
  #if defined(PROBOT_BATTERY_INA_SDA) != defined(PROBOT_BATTERY_INA_SCL)
  #error "[PB-E105] I2C pinleri yarim tanimli: PROBOT_BATTERY_INA_SDA ve PROBOT_BATTERY_INA_SCL birlikte verilmeli (ya ikisi ya hicbiri). Docs: probotstudio.com/docs/hatalar/#pb-e105"
  #endif
  #if defined(PROBOT_BATTERY_INA_SDA)
  static_assert((PROBOT_BATTERY_INA_SDA) != (PROBOT_BATTERY_INA_SCL),
    "[PB-E105] SDA ve SCL ayni pin olamaz. Docs: probotstudio.com/docs/hatalar/#pb-e105");
    #ifdef PROBOT_ESTOP_ENABLE_PIN
    static_assert((PROBOT_BATTERY_INA_SDA) != (PROBOT_ESTOP_ENABLE_PIN) &&
                  (PROBOT_BATTERY_INA_SCL) != (PROBOT_ESTOP_ENABLE_PIN),
      "[PB-E105] INA I2C pini PROBOT_ESTOP_ENABLE_PIN ile cakisiyor — acil durdurma hatti I2C'ye devredilemez. Docs: probotstudio.com/docs/hatalar/#pb-e105");
    #endif
    #ifdef PROBOT_RSL_PIN
    static_assert((PROBOT_BATTERY_INA_SDA) != (PROBOT_RSL_PIN) &&
                  (PROBOT_BATTERY_INA_SCL) != (PROBOT_RSL_PIN),
      "[PB-E105] INA I2C pini PROBOT_RSL_PIN ile cakisiyor. Docs: probotstudio.com/docs/hatalar/#pb-e105");
    #endif
  #endif
#else
  #define PROBOT_BATTERY_ENABLED 0
#endif

#ifndef PROBOT_BATTERY_INA_ADDR
#define PROBOT_BATTERY_INA_ADDR 0x40
#endif
#ifndef PROBOT_BATTERY_INA_SHUNT_MOHM
#define PROBOT_BATTERY_INA_SHUNT_MOHM 100
#endif
#ifndef PROBOT_BATTERY_TRIM
#define PROBOT_BATTERY_TRIM 1.0f
#endif

#if PROBOT_BATTERY_ENABLED
static_assert(PROBOT_BATTERY_TRIM >= 0.5f && PROBOT_BATTERY_TRIM <= 2.0f,
  "[PB-E105] PROBOT_BATTERY_TRIM makul araligin disinda (0.5-2.0) — trim ince ayar icindir, oran duzeltmesi icin bolucu direnclerini duzeltin. Docs: probotstudio.com/docs/hatalar/#pb-e105");
static_assert((PROBOT_BATTERY_INA_SHUNT_MOHM) > 0,
  "[PB-E105] PROBOT_BATTERY_INA_SHUNT_MOHM pozitif olmali (mili-ohm). Docs: probotstudio.com/docs/hatalar/#pb-e105");
static_assert((PROBOT_BATTERY_INA_ADDR) >= 0x08 && (PROBOT_BATTERY_INA_ADDR) <= 0x77,
  "[PB-E105] PROBOT_BATTERY_INA_ADDR gecerli 7-bit I2C adresi olmali (0x08-0x77). Docs: probotstudio.com/docs/hatalar/#pb-e105");
#endif

#if PROBOT_BATTERY_ENABLED && defined(ESP32)
#include <Arduino.h>
#include <probot/core/core_config.hpp>
#include <probot/telemetry/telemetry.hpp>
#if PROBOT_BATTERY_SRC_INA
#include <Wire.h>
#endif
#endif

namespace probot::io::battery {

namespace detail {
  // Saf yardımcılar — host testlerinde de derlenir.

  // EMA: ilk örnekte doğrudan başla (0'dan yavaş tırmanma olmasın).
  inline float emaStep(float prev, float sample, float alpha) {
    if (prev <= 0.0f) return sample;
    return prev + alpha * (sample - prev);
  }

  // State'e yazım eşiği: 0.1 V çözünürlük. Her ADC örneği state seq'ini
  // oynatıp 'S' frame yağmuruna yol açmasın diye yalnız desivolt değişince
  // yayınlanır.
  inline unsigned toDecivolts(float v) {
    if (!(v > 0.0f)) return 0;
    if (v > 99.9f) v = 99.9f;
    return (unsigned)(v * 10.0f + 0.5f);
  }

  // Bölücü oranı: Vbat = Vadc * (Rtop + Rbot) / Rbot
  inline float dividerRatio(float rTopK, float rBotK) {
    if (!(rBotK > 0.0f)) return 0.0f;
    return (rTopK + rBotK) / rBotK;
  }
}

#if PROBOT_BATTERY_ENABLED
#ifdef ESP32

namespace detail {
  inline float    g_ema        = 0.0f;
  inline unsigned g_lastPubDv  = ~0u;
  inline uint32_t g_lastPollMs = 0;
  inline bool     g_inited     = false;
  inline float    g_currentA   = 0.0f;   // INA varken son akım örneği
  inline uint8_t  g_failStreak = 0;      // ardışık I2C hatası
  inline bool     g_faultAnnounced = false;

  constexpr uint32_t POLL_MS   = 250;
  // Poll less often while the sensor is down: every failed attempt costs an
  // I2C timeout (~20 ms) of bus time.
  constexpr uint32_t POLL_FAULT_MS = 5000;
  // Battery task tick. Below the supervisor so it can never starve it.
  constexpr uint32_t TASK_TICK_MS = 50;
  // Same as the sysloop that used to run this code: failed I2C transactions
  // go through ESP-IDF error logging plus Serial and telemetry printing.
  constexpr uint32_t TASK_STACK   = 4096;
  inline TaskHandle_t g_task = nullptr;
  constexpr float    EMA_ALPHA = 0.2f;
  constexpr uint8_t  FAIL_LIMIT = 4;
}

#if PROBOT_BATTERY_SRC_INA
namespace detail {
  inline bool inaRead16(uint8_t reg, uint16_t& out) {
    Wire.beginTransmission((uint8_t)PROBOT_BATTERY_INA_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)PROBOT_BATTERY_INA_ADDR, 2) != 2) return false;
    out = ((uint16_t)Wire.read() << 8) | (uint16_t)Wire.read();
    return true;
  }

  inline bool inaWrite16(uint8_t reg, uint16_t val) {
    Wire.beginTransmission((uint8_t)PROBOT_BATTERY_INA_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(val >> 8));
    Wire.write((uint8_t)(val & 0xFF));
    return Wire.endTransmission() == 0;
  }

  // Config register'ını bilinen sürekli-örnekleme değerine yaz (POR
  // default'unun aynısı). Sensör önceki bir yazılım tarafından power-down'a
  // alınıp enerjili kalmışsa ACK verip bayat/0 okutabilirdi — varsayma, yaz.
  inline void inaConfigure() {
#if PROBOT_BATTERY_INA == 219
    inaWrite16(0x00, 0x399F);
#else
    inaWrite16(0x00, 0x4127);
#endif
  }

  // Bus gerilimi (V). INA219: reg 0x02, LSB 4 mV, değer >>3.
  // INA226: reg 0x02, LSB 1.25 mV. İkisi de POR default konfigürasyonda
  // sürekli örnekleme yapar — config yazmaya gerek yok.
  inline bool inaBusVolts(float& out) {
    uint16_t raw;
    if (!inaRead16(0x02, raw)) return false;
#if PROBOT_BATTERY_INA == 219
    out = (float)(raw >> 3) * 0.004f;
#else
    out = (float)raw * 0.00125f;
#endif
    return true;
  }

  // Shunt gerilimi → akım (A). Kalibrasyon register'ı gerekmez; yazılımda
  // I = Vshunt / Rshunt. INA219 LSB 10 µV, INA226 LSB 2.5 µV (signed).
  inline bool inaCurrentAmps(float& out) {
    uint16_t raw;
    if (!inaRead16(0x01, raw)) return false;
#if PROBOT_BATTERY_INA == 219
    float vShunt = (float)(int16_t)raw * 10e-6f;
#else
    float vShunt = (float)(int16_t)raw * 2.5e-6f;
#endif
    out = vShunt / ((float)PROBOT_BATTERY_INA_SHUNT_MOHM / 1000.0f);
    return true;
  }
}
#endif // PROBOT_BATTERY_SRC_INA

namespace detail {
  inline void inaConfigureIfIna() {
#if PROBOT_BATTERY_SRC_INA
    inaConfigure();
#endif
  }

  inline void beginOnce() {
    if (g_inited) return;
    g_inited = true;
#if PROBOT_BATTERY_SRC_ADC
    analogSetPinAttenuation((uint8_t)PROBOT_BATTERY_ADC_PIN, ADC_11db);
#else
  #if defined(PROBOT_BATTERY_INA_SDA) && defined(PROBOT_BATTERY_INA_SCL)
    Wire.begin(PROBOT_BATTERY_INA_SDA, PROBOT_BATTERY_INA_SCL);
  #else
    Wire.begin();
  #endif
    Wire.setClock(100000);
    Wire.setTimeOut(20); // bound each transaction when the sensor is absent
    inaConfigure();
#endif
  }

  // Tek örnek al (V). false = kaynak okunamadı.
  inline bool sampleVolts(float& out) {
#if PROBOT_BATTERY_SRC_ADC
    // 8 örnek ortalama; analogReadMilliVolts eFuse kalibrasyonu uygular.
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) acc += analogReadMilliVolts((uint8_t)PROBOT_BATTERY_ADC_PIN);
    float vAdc = (float)(acc / 8) / 1000.0f;
    out = vAdc * dividerRatio((float)PROBOT_BATTERY_R_TOP_K, (float)PROBOT_BATTERY_R_BOT_K);
    return true;
#else
    float v;
    if (!inaBusVolts(v)) return false;
    float a;
    g_currentA = inaCurrentAmps(a) ? a : 0.0f; // okunamayan akım bayat kalmasın
    out = v;
    return true;
#endif
  }
}

// Called only from the battery task; all battery I2C/ADC access happens there.
inline void poll(uint32_t now_ms) {
  using namespace detail;
  uint32_t interval = (g_failStreak >= FAIL_LIMIT) ? POLL_FAULT_MS : POLL_MS;
  if ((uint32_t)(now_ms - g_lastPollMs) < interval) return;
  g_lastPollMs = now_ms;
  beginOnce();

  float v;
  if (!sampleVolts(v)) {
    if (g_failStreak < 255) g_failStreak++;
    if (g_failStreak == FAIL_LIMIT) {
      // Kaynak düştü: arayüz "Veri yok"a dönsün, bir kez duyurulsun.
      g_ema = 0.0f;
      g_currentA = 0.0f;
      probot::robot::state().setBatteryVoltage(now_ms, 0.0f);
      g_lastPubDv = 0;
      if (!g_faultAnnounced) {
        g_faultAnnounced = true;
        probot::telemetry::println("!! [PB-E306] BATTERY SENSOR UNREACHABLE — INA I2C yanit vermiyor — docs/hatalar#pb-e306");
        Serial.println("[BATT ] [PB-E306] sensor unreachable — docs: probotstudio.com/docs/hatalar/#pb-e306");
      }
    }
    return;
  }
  if (g_failStreak >= FAIL_LIMIT) {
    Serial.println("[BATT ] sensor recovered");
    inaConfigureIfIna(); // arıza sonrası sensör taze takılmış olabilir
  }
  g_failStreak = 0;
  g_faultAnnounced = false;

  g_ema = emaStep(g_ema, v * PROBOT_BATTERY_TRIM, EMA_ALPHA);
  // Yayın histerezisi: yalnız desivolt yuvarlaması değil, son yayından
  // >=0.06 V uzaklaşma da şart — 12.35 V sınırında 123↔124 flip-flop'u
  // (ve her flip'in tetiklediği S frame'i) engellenir.
  unsigned dv = toDecivolts(g_ema);
  bool first = (g_lastPubDv == ~0u);
  float distDv = g_ema * 10.0f - (float)g_lastPubDv;
  if (distDv < 0) distDv = -distDv;
  if (first || (dv != g_lastPubDv && distDv >= 0.6f)) {
    g_lastPubDv = dv;
    probot::robot::state().setBatteryVoltage(now_ms, (float)dv / 10.0f);
  }
}

namespace detail {
  inline void batteryTask(void*) {
    for (;;) {
      poll(millis());
      vTaskDelay(pdMS_TO_TICKS(TASK_TICK_MS));
    }
  }
}

// Started once by runtime_setup(). Priority 1 on core 0: below the sysloop
// supervisor (PRIO_CTRL) and not subscribed to the task watchdog, so a hung
// bus stalls only battery readings.
inline void startTask() {
  if (detail::g_task) return;
  if (xTaskCreatePinnedToCore(detail::batteryTask, "battery", detail::TASK_STACK,
                              NULL, PRIO_USER, &detail::g_task, CORE_UI) != pdPASS) {
    detail::g_task = nullptr;
    Serial.println("[BATT ] battery task could not be created (out of memory): no automatic battery readings");
  }
}

// Anlık akım (A). Yalnız INA kaynağında anlamlı; diğer durumda 0.
// (+ = bataryadan çekiş; INA'nın VIN+ batarya, VIN- yük yönünde bağlıysa.)
inline float currentAmps() { return detail::g_currentA; }

#endif // ESP32
#endif // PROBOT_BATTERY_ENABLED

} // namespace probot::io::battery

#ifdef ESP32
namespace probot {
  // Elle besleme — kendi ölçümünüz varsa. Arayüzdeki gösterge ve 'S'
  // frame'indeki batt alanı bu değeri okur. PROBOT_BATTERY_ADC_PIN ya da
  // PROBOT_BATTERY_INA tanımlıyken KULLANMAYIN: otomatik okuma ~250 ms'de
  // bir üzerine yazar. Değer aynı desivolta yuvarlanıyorsa yazılmaz —
  // loop'tan her turda çağırmak S frame trafiği üretmez.
  inline void setBatteryVoltage(float volts) {
    auto s = probot::robot::state().read();
    if (probot::io::battery::detail::toDecivolts(volts) ==
        probot::io::battery::detail::toDecivolts(s.batteryVoltage)) return;
    probot::robot::state().setBatteryVoltage(millis(), volts);
  }
}
#endif
