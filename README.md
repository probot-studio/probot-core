# Probot

ESP32 tabanlı robot yarışması iletişim kütüphanesi. Robot bir WiFi
erişim noktası açar, tarayıcıdan çalışan Driver Station arayüzü sunar
ve joystick verisini WebSocket ile düşük gecikmeyle robota taşır.

**Sürüm 0.3.0** · ESP32 / ESP32-S3 · [API Referansı](API.md) ·
[English summary below](#probot-en)

---

## Kurulum

1. **Kütüphane:** Arduino IDE → Library Manager → **"probot"** ara → Install.
   Ya da en güncel sürüm için:
   ```bash
   git clone https://github.com/probot-studio/probot-core ~/Arduino/libraries/probot-core
   ```
2. **ESP32 core:** Boards Manager → "esp32" (Espressif) → **3.x** kurulu olmalı.
3. **Kart:** `ESP32S3 Dev Module` (veya `ESP32 Dev Module`).
4. **Partition:** Tools → Partition Scheme → **Huge APP (3MB No OTA)**.
   Bu ayar şart — varsayılan bölüm yetersiz, derleme sığmaz.

## İlk robot (5 dakika)

```cpp
#define PROBOT_WIFI_AP_SSID     "MyRobot"
#define PROBOT_WIFI_AP_PASSWORD "robot1234"   // en az 8 karakter
#define PROBOT_WIFI_AP_CHANNEL  1             // 1-13 (yarışmada elle dağıtın)
#include <probot.h>

void robotInit() {}                // Init'e basınca 1 kez
void robotEnd() {}                 // Stop'ta 1 kez — motorları burada durdur
void teleopInit() {}               // teleop başlarken 1 kez

void teleopLoop() {                // ~50 Hz tekrar çağrılır
  auto js = probot::io::joystick_api::makeDefault();
  float ileri = js.getLeftY();     // -1..+1 (ileri pozitif)
  bool  buton = js.getA();
  // motor kodun burada
  delay(20);
}

void autonomousInit() {}
void autonomousLoop() { delay(100); }
```

> `setup()` ve `loop()` **tanımlamayın** — kütüphane kendisi tanımlar.
> Altı fonksiyonun altısı da sketch'te bulunmak zorundadır.

1. Yükle → Serial monitörde IP'yi gör (`192.168.4.1`).
2. Tablet/telefonu `MyRobot` WiFi ağına bağla — karşılama sayfası
   kendiliğinden açılır (captive portal).
3. Açılmazsa tarayıcıda `http://192.168.4.1` aç.
4. Kumandayı tablete bağla (USB/Bluetooth) → **Init** → **Start**.

## Örnekler

| Örnek | Ne yapar |
|---|---|
| `JoystickTest` | Eksen/buton değerlerini Serial'e ve telemetri paneline basar. İlk deneme için. |
| `TankDrive` | Çift motor tank sürüşü (BTS7960/IBT-2 tarzı sürücü). Motor kodunun şablonu. |
| `ServoTest` | Joystick ile servo kontrolü — titreşimsiz servo kullanımının doğru yolu. |
| `AutonomousChooser` | Derlenmiş sol/sağ otonom rutinler arasında DS'ten seçim yapar; seçenekler arasında geçişte yeniden yükleme gerekmez. |

## Ayar makroları

Hepsi `#include <probot.h>` satırından **önce** tanımlanır:

| Makro | Varsayılan | Açıklama |
|---|---|---|
| `PROBOT_WIFI_AP_SSID` | `Probot-XXXXXX` | AP adı. Tanımsız bırakılırsa MAC eki otomatik açılır. Ek açıkken en fazla 25, kapalıyken 32 karakter |
| `PROBOT_WIFI_AP_PASSWORD` | — (zorunlu) | AP şifresi (≥8 karakter) |
| `PROBOT_WIFI_AP_CHANNEL` | — (zorunlu) | AP kanalı, **1-13**. Yarışmada robotlara elle farklı kanal verin |
| `PROBOT_WIFI_AUTO_CHANNEL` | `0` | `1`: robot açılışta bandı tarayıp en boş kanalı **kendi** seçer. **Filoda önerilmez** (bkz. kanal planı), sadece tek robot için |
| `PROBOT_WIFI_AP_SSID_MAC_SUFFIX` | kapalı | SSID sonuna `-XXXXXX` (MAC) ekler |
| `PROBOT_DS_TIMEOUT_MS` | `10000` | DS'ten veri kesilirse timeout (ms) |
| `PROBOT_DS_TIMEOUT_FORCE_STOP` | `1` | `1`: timeout'ta robot STOP. `0`: loop sürer, joystick nötr, bağlantı dönünce devam |
| `PROBOT_DS_OWNER_TIMEOUT_MS` | `5000` | Sahip client sessiz kalırsa slotun boşalma süresi |
| `PROBOT_INPUT_TIMEOUT_MS` | `500` | Joystick verisi kesilince eksenlerin sıfırlanma süresi |
| `PROBOT_WIFI_ENABLE_11B` | `0` | `1`: 802.11b hızlarını aç (sadece 2010 öncesi cihazlar için; beacon airtime'ını 6 kat artırır) |
| `PROBOT_WIFI_PMF_REQUIRED` | `0` | `1`: PMF (802.11w) zorunlu — deauth sahteciliğine karşı koruma, eski tabletlerle uyumsuz olabilir |
| `PROBOT_CAPTIVE_PORTAL` | `1` | Ağa katılan cihazda karşılama sayfası kendiliğinden açılır; `0` kapatır |
| `NEOPIXEL_PIN` / `NEOPIXEL_COUNT` | `3` / `1` | Durum LED'i pini/adedi |
| `PROBOT_LOOP_DEADLINE_MS` | `2000` | Loop turu bu süreyi aşarsa "stalled": input sıfır, halt-safe (öldürme/reboot yok) |
| `PROBOT_WDT_TIMEOUT_S` | `8` | Donanım watchdog (yalnız sysloop; bir *kütüphane* kilidi reboot ettirir, kullanıcı kodu değil) |
| `PROBOT_ESTOP_ENABLE_PIN` | `-1` | Kütüphanenin sürdüğü enable GPIO'su (motor sürücü enable / kontaktör). Boot'ta HIGH, acil durdurmada LOW |
| `PROBOT_ESTOP_END_MS` | `500` | Acil durdurmada `robotEnd()`'e tanınan süre; aşılırsa çip reboot eder |
| `PROBOT_RSL_PIN` | `-1` | Sinyal lambası (RSL) digital pini: hareket edebilirken blink, yoksa sabit açık |
| `NEOPIXEL_BRIGHTNESS` | `32` | Durum LED'i parlaklığı (0-255) |

## Yarışma günü: kanal planı

- 2.4 GHz'te birbirini **ezmeyen** kanallar: **1, 5, 9, 13**. Aynı anda
  çalışan robotları bu kanallara **elle** dağıtın — her robota
  `PROBOT_WIFI_AP_CHANNEL` ile sabit ve farklı bir kanal verin.
  Deterministik atama, koordineli bir filoda en güvenli yöntemdir.
- Kanal yarışma günü **yeniden flash gerektirmeden** değiştirilebilir:
  Logs sayfası → Kanal Değiştir. 1-13 arası seçimler CSA ile canlı
  uygulanır (uyumlu istemciler bağlantıyı koparmadan takip eder) ve
  kalıcı kaydedilir. Maç **sırasında** değiştirmeyin — bazı tabletler
  CSA'yı takip etmeyip birkaç saniye kopabilir.
- Bazı dizüstü/tablet'ler bölge kilidi yüzünden **kanal 12-13'ü
  görmez**. Bir cihaz robotu bulamıyorsa o robota 1-11 arası bir kanal
  verin.
- **Otomatik kanal seçimi (`PROBOT_WIFI_AUTO_CHANNEL 1`) varsayılan
  KAPALIDIR ve filoda önerilmez.** Her robot bandı bağımsız tarar;
  robotlar aynı anda açıldığında hiçbiri henüz yayın yapmadığından
  bandı boş görür ve **hepsi aynı kanala (kanal 1) düşebilir** —
  dağıtmak yerine yığar. Yalnızca ortamda tek robot varken
  (ev/atölye) mantıklıdır. Seçilen kanal Serial'de ve Logs sayfasında
  görünür.
- Telefon hotspot'ları ve seyirci cihazları da 2.4 GHz'i doldurur —
  maç sırasında robot çevresinde hotspot açtırmayın.
- Sinyal sorunlarını sahada ayıklamak için `/health` endpoint'i RSSI
  verir; -70 dBm'den kötüyse mesafe/anten sorununa bakın.

## Servo kullanımı (titreme çözümü)

Servo titremesinin iki yaygın sebebi var; ikisi de kütüphane dışında:

1. **Timer çakışması:** `analogWrite` (motorlar, ~1 kHz) ile servo (50 Hz)
   aynı LEDC timer'ına düşerse biri diğerinin frekansını bozar. probot bir
   servo sınıfı vermez (donanımı sen sürersin); titremeyi önlemek için
   servoya **yüksek bir LEDC kanalı** verin — motorların `analogWrite`'ı
   alttan (0,1,2…) kullandığı için çakışmaz:
   ```cpp
   #define SERVO_PIN 4
   void robotInit() { ledcAttachChannel(SERVO_PIN, 50, 14, 7); } // 50 Hz, 14-bit, kanal 7
   void teleopLoop() {
     uint16_t us = 500 + (angle/180.0f)*2000;        // 0-180° -> 500-2500 µs
     ledcWrite(SERVO_PIN, (uint32_t)us * 16383 / 20000);
   }
   void robotEnd() { ledcWrite(SERVO_PIN, 0); }      // darbeyi kes
   ```
   Tam örnek: `examples/ServoTest`.
2. **Güç:** Servoyu ESP32'nin 5V/3V3 pininden beslemeyin. WiFi anlık
   akım çekişleri gerilimi düşürür, servo seğirir. Servoya **ayrı 5-6V
   kaynak (BEC/UBEC)** verin, toprakları ortak bağlayın.

PCA9685 kullanıyorsanız: servo çıkışları için PWM frekansı **50 Hz**
olmalı (1 kHz'te servo darbe genişliği fiziksel olarak üretilemez).

## Durum LED'i ve RSL

Builtin NeoPixel **yalnız maç durumunu** gösterir, rengini kütüphane sürer —
elle renk atama API'si yoktur (LED'in rengi hep bir anlam taşır).

| Renk | Anlam |
|---|---|
| Mavi sabit | Açık, DS bağlı değil |
| Mavi yanıp sönüyor | DS bağlı, Init bekleniyor |
| Sarı sabit | Init tamam, Start bekleniyor |
| Turuncu yanıp sönüyor | Otonom çalışıyor |
| Yeşil yanıp sönüyor | Teleop çalışıyor |
| Kırmızı yanıp sönüyor | Stalled — loop 2 sn'den uzun döndü, güvende tutuluyor |
| Kırmızı sabit | Acil durdurma (kilitli, reboot gerekli) |

**RSL (sinyal lambası):** `#define PROBOT_RSL_PIN <gpio>` verirseniz kütüphane
o digital pini sürer — robot **hareket edebilirken** (teleop/otonom) yanıp
söner, aksi halde **sabit açık** kalır.

## Bağlantı davranışı (güvenlik)

- Joystick verisi **500 ms** kesilirse eksen/butonlar otomatik sıfırlanır
  → motorlar son komutla kaçmaz.
- DS **10 sn** tamamen sessiz kalırsa robot STOP'a geçer
  (`PROBOT_DS_TIMEOUT_FORCE_STOP 0` ile yumuşak moda alınabilir).
- Aynı anda **tek client** kontrol edebilir (ilk bağlanan IP sahip olur).
  İkinci cihaz arayüzü açarsa `403` alır. `/health` ve `/info` ise
  sahiplik gerektirmez — hakem/izleme cihazları serbestçe okuyabilir.

## Yaşam döngüsü ve loop sözleşmesi (0.3.0)

- Altı hook **tek kalıcı task'ta**, yalnız döngü sınırlarında çalışır.
  Stop/faz değişimi kullanıcı kodunu iş ortasında **kesmez** — bu yüzden
  bir Wire/I2C ya da malloc kilidi asla orphan olmaz (eski sürümlerdeki
  donmanın kök sebebi buydu).
- **Kural:** her `teleopLoop`/`autonomousLoop` turu bir gün **dönmeli**
  (öneri < ~2 sn). Blocking serbest, *sonsuz* blocking yasak. I2C/sensör
  çağrılarına timeout koyun — örn. `Wire.begin()` sonrası
  `Wire.setTimeOut(50);` — yoksa takılı bir cihaz turu kilitler.
- **Stop kooperatiftir:** o anki tur dönünce `robotEnd()` koşar (en fazla
  bir loop periyodu gecikme). Anında kesme için acil durdurma kullanın.
- **Stall (halt-safe):** bir tur `PROBOT_LOOP_DEADLINE_MS` (2 sn) içinde
  dönmezse input sıfırlanır, LED kırmızı yanar, robot güvende tutulur —
  **task öldürülmez, çip reboot edilmez** (homing/relative state korunur).

## Acil durdurma

- Arayüzdeki kırmızı **EMERGENCY STOP** butonu (ya da
  `/robotControl?cmd=estop`) kullanıcı task'ını öldürür, `robotEnd()`'i
  watchdog'lu çalıştırır ve robotu **reboot'a kadar kilitler** (Init/Start
  reddedilir; "Reboot" butonu ya da güç döngüsü temizler). Donmuş bir
  loop'u bile durdurur.
- Gerçek güvenlik garantisi için **donanım E-stop**'unu güç/enable hattına
  koyun: çip tamamen kilitlense bile çalışan tek katman odur. Kütüphanenin
  `PROBOT_ESTOP_ENABLE_PIN`'ini motor sürücülerinin enable hattına
  bağlarsanız acil durdurma o hattı da donanımda keser.

## Yapay zeka ile kod yazma

Gemini / ChatGPT / Claude'a robot kodu yazdırırken bu satırları
prompt'unuzun başına ekleyin:

```text
ESP32 için "probot" kütüphanesiyle (0.3.0) Arduino kodu yaz.
Önce API referansını oku:
https://raw.githubusercontent.com/probot-studio/probot-core/stable/API.md
Kurallar:
- setup()/loop() TANIMLAMA; robotInit, robotEnd, teleopInit, teleopLoop,
  autonomousInit, autonomousLoop — altısı da tanımlı olacak.
- Joystick: auto js = probot::io::joystick_api::makeDefault();
  js.getLeftY() vb. (-1..+1). probot::io::gamepad() üzerinde getLeftX gibi
  metodlar YOKTUR.
- Servo için ham LEDC kullan: robotInit'te ledcAttachChannel(pin,50,14,7)
  (yüksek kanal → motor analogWrite'ıyla çakışmaz), teleopLoop'ta ledcWrite.
- teleopLoop ~50 Hz çağrılır; içinde sonsuz döngü/uzun blocking yapma.
```

Makine-okur özet: [`llms.txt`](llms.txt) · Tam referans: [`API.md`](API.md)

## Sık sorunlar

| Belirti | Çözüm |
|---|---|
| "Sketch too big" | Partition Scheme → Huge APP (3MB No OTA) |
| `#error ... PASSWORD` | Makroları `#include <probot.h>`'den önce yazın |
| Arayüz açılmıyor / 403 | Başka bir cihaz bağlı (tek client kuralı). Diğerini kapatın, ~5 sn bekleyin |
| Joystick görünmüyor | Kumandada herhangi bir tuşa basın (tarayıcı gamepad'i tuşa basılınca tanır) |
| Sık kopma | Kanal çakışması — robotları 1/5/9/13'e **elle** dağıtın (her birine farklı sabit kanal) |
| Servo titriyor | Yukarıdaki "Servo kullanımı" bölümü |

## Destek ve lisans

- Hata bildirimi: https://github.com/probot-studio/probot-core/issues
- WhatsApp: +90 538 040 81 48
- Lisans: MIT + Commons Clause — eğitim ve yarışma kullanımı ücretsiz,
  ticari lisans için tunagul54@gmail.com

---

# Probot (EN)

ESP32 communication library for educational robotics competitions:
the robot hosts a WiFi AP and a browser-based driver station; joystick
input streams over a binary WebSocket at 50 Hz with automatic failsafes
(input zeroing after 500 ms, robot stop after 10 s of DS silence).

**Install:** Arduino IDE Library Manager → "probot", or clone
https://github.com/probot-studio/probot-core into `~/Arduino/libraries/`.
Requires arduino-esp32 core 3.x and the **Huge APP (3MB No OTA)**
partition scheme.

**Minimal sketch:** see the Turkish quick start above — the code is
identical. Define the three `PROBOT_WIFI_*` macros, include `probot.h`,
implement the six lifecycle hooks (`robotInit`, `robotEnd`,
`teleopInit`, `teleopLoop`, `autonomousInit`, `autonomousLoop`), and
read input via `probot::io::joystick_api::makeDefault()`. Do not define
`setup()`/`loop()` — the library owns them.

Full API reference: [API.md](API.md) · Machine-readable index:
[llms.txt](llms.txt) · Changes: [CHANGELOG.md](CHANGELOG.md)
