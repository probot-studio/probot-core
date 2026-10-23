# Probot

ESP32 tabanlı robot yarışması iletişim kütüphanesi. Robot bir WiFi
erişim noktası açar, tarayıcıdan çalışan Driver Station arayüzü sunar
ve joystick verisini WebSocket ile düşük gecikmeyle robota taşır.

**dev (0.4.0 adayı)** · ESP32 / ESP32-S3 · [Dokümanlar](https://probotstudio.com/docs) ·
[API Referansı](API.md) · [English summary below](#probot-en)

---

## Kurulum

1. **Kütüphane:** Arduino IDE → Library Manager → **"probot"** ara → Install.
   Ya da en güncel sürüm için:
   ```bash
   git clone https://github.com/probot-studio/probot-core ~/Arduino/libraries/probot-core
   ```
2. **ESP32 core:** Boards Manager → "esp32" (Espressif) → **3.x** kurulu olmalı.
3. **Kart:** `ESP32S3 Dev Module`; Tools ayarları aşağıda (Kart ayarları).
4. **Partition:** Tools → Partition Scheme → **Huge APP (3MB No OTA)**.
   Bu ayar şart — varsayılan bölüm yetersiz, derleme sığmaz.

### Kart ayarları

Önerilen kart Boardoza Pulse S32-S3 (ESP32-S3-WROOM-1-N4R2: 4 MB flash,
2 MB quad PSRAM, doğrudan USB-C). Tools menüsü:

| Ayar | Değer |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | QSPI PSRAM |
| USB Mode | Hardware CDC and JTAG |
| USB CDC On Boot | Enabled |

USB CDC On Boot kapalıyken `Serial` (kütüphanenin açılış yazısı, WiFi adı
ve kayıtları) USB-C'ye değil UART0'a (J4'te IO43/IO44) gider. Açıkken ve Arduino-ESP32
3.3.9 ya da sonrasında, port açık tutulup okunmadığında bir `Serial`
yazması en çok ~100 ms bekler; sığmayan metin atılır. Bu ayarların `arduino-cli` FQBN'i:
`esp32:esp32:esp32s3:FlashSize=4M,PartitionScheme=huge_app,PSRAM=enabled,CDCOnBoot=cdc`.

Kütüphanenin kullandığı ya da seçeneklerine uyan Pulse pinleri (kartın
üzerindeki baskıdan):

| Pin | Kullanım |
|---|---|
| IO3 | Karttaki RGB LED: durum LED'i (`NEOPIXEL_PIN`, varsayılan 3) |
| IO1 (SDA) / IO2 (SCL), J3 | I2C soketi: INA batarya sensörünün varsayılan pinleri; başka I2C cihazları da paylaşabilir |
| IO7 (J5 CS), IO10 (J4) | `PROBOT_BATTERY_ADC_PIN` bölücüsü için ADC1 pinleri; J3 I2C için kullanılmıyorsa IO1 / IO2 de. Varsayılan yok: kartın batarya girişinin (J1) bilinen bir ölçüm pini yok |
| IO47 / IO48 (J8), IO35-IO38 (J6) | `PROBOT_RSL_PIN` ya da `PROBOT_ESTOP_ENABLE_PIN` için boş GPIO (ikisi de varsayılan kapalı) |
| IO0, IO45, IO46 | Strapping pinleri (LED'in pini IO3 gibi): RSL ya da acil durdurma hattı için kullanmayın |
| IO43 / IO44 | UART0 (USB CDC On Boot kapalıyken seri konsol) |
| IO19 / IO20 | Doğrudan USB |

## İlk robot (5 dakika)

```cpp
#define PROBOT_WIFI_AP_SSID     "MyRobot"
#define PROBOT_WIFI_AP_PASSWORD "robot1234"   // en az 8 karakter
#define PROBOT_WIFI_AP_CHANNEL  1             // 1-13 (yarışmada elle dağıtın)
#include <probot.h>

void teleopInit() {}               // TeleOp seçiliyken INIT'e basınca 1 kez

void teleopLoop() {                // ~50 Hz tekrar çağrılır
  auto js = probot::io::joystick_api::makeDefault();
  float ileri = js.getLeftY();     // -1..+1 (ileri pozitif)
  bool  buton = js.getA();
  // motor kodun burada
}
void teleopStop() {}               // TeleOp'tan her çıkışta güvenli durdur

void autonomousInit() {}
void autonomousLoop() {}
void autonomousStop() {}           // Auto'dan her çıkışta güvenli durdur
```

> `setup()` ve `loop()` **tanımlamayın** — kütüphane kendisi tanımlar.
> Dört güvenlik hook'u (`autonomousLoop/Stop`, `teleopLoop/Stop`) zorunludur.
> `init` hook'ları opsiyoneldir; ana iskelette donanım hazırlığı için gösterilir.

1. Yükle → Serial monitörde IP'yi gör (`192.168.4.1`).
2. Tablet/telefonu `MyRobot` WiFi ağına bağla — karşılama sayfası
   kendiliğinden açılır (captive portal).
3. Açılmazsa tarayıcıda `http://192.168.4.1` aç.
4. Kumandayı tablete bağla (USB/Bluetooth) → modu seç → **Init** → **Start**.

## Örnekler

| Örnek | Ne yapar |
|---|---|
| `JoystickTest` | Eksen/buton değerlerini Serial'e ve telemetri paneline basar. İlk deneme için. |
| `TankDrive` | Çift motor tank sürüşü (BTS7960/IBT-2 tarzı sürücü). Motor kodunun şablonu. |
| `ServoTest` | Joystick ile servo kontrolü — titreşimsiz servo kullanımının doğru yolu. |

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
| `PROBOT_LOOP_DEADLINE_MS` | `2000` | InitLoop/loop turu bu süreyi aşarsa "stalled": input sıfır, halt-safe (öldürme/reboot yok) |
| `PROBOT_HOOK_DEADLINE_MS` | `10000` | Tek seferlik init/start/stop hook'ları için aynı stall sınırı (loop deadline'ından küçük olamaz) |
| `PROBOT_WDT_TIMEOUT_S` | `8` | Donanım watchdog (yalnız sysloop; bir *kütüphane* kilidi reboot ettirir, kullanıcı kodu değil) |
| `PROBOT_ESTOP_ENABLE_PIN` | `-1` | Kütüphanenin sürdüğü enable GPIO'su (motor sürücü enable / kontaktör). Boot'ta HIGH, acil durdurmada LOW |
| `PROBOT_ESTOP_END_MS` | `500` | Acil durdurmada aktif OpMode `stop()`una tanınan süre; aşılırsa çip reboot eder |
| `PROBOT_RSL_PIN` | `-1` | Sinyal lambası (RSL) digital pini: hareket edebilirken blink, yoksa sabit açık |
| `PROBOT_BATTERY_ADC_PIN` | kapalı | Batarya ölçümü, yöntem 1: gerilim bölücünün orta ucu. **ADC1 pini şart (GPIO1-10)** — ADC2 WiFi açıkken çalışmaz [PB-E104] |
| `PROBOT_BATTERY_R_TOP_K` / `_R_BOT_K` | — | Bölücü dirençleri, kΩ (batarya tarafı / GND tarafı). 3S için öneri: 100k/22k → 12.6 V'ta 2.27 V |
| `PROBOT_BATTERY_INA` | kapalı | Batarya ölçümü, yöntem 2: I2C sensör — `219` ya da `226`. ADC yöntemiyle birlikte kullanılamaz [PB-E105] |
| `PROBOT_BATTERY_INA_ADDR` | `0x40` | INA I2C adresi |
| `PROBOT_BATTERY_INA_SDA` / `_SCL` | `1` / `2` | INA için I2C pinleri (Pulse J3 soketi). İkisi birlikte verilir ya da hiçbiri [PB-E105] |
| `PROBOT_BATTERY_TRIM` | `1.0f` | Multimetreyle ince ayar çarpanı (0.5-2.0) |
| `USER_LOOP_PERIOD_MS` | `20` | InitLoop/loop sabit hızlı çağrı periyodu (~50 Hz), 1-1000; çalışırken `probot::setLoopPeriodMs()` |
| `PROBOT_USER_STACK` | `8192` | User task stack'i, bayt (en az 4096) [PB-E107] |
| `PROBOT_DASH_MAX_TOPICS` / `_MAX_VALUE` / `_MAX_SCHEMA_BYTES` / `_POOL_BYTES` | `128` / `1024` / `4096` / `32768` | Dashboard topic deposu sınırları (ileri seviye, bkz. API.md) [PB-E108] |
| `PROBOT_CONSOLE_BYTES` | `4096` | Robot konsolu tamponu, bayt (2048-65536); dolunca en eski satırlar düşer [PB-E110] |
| `NEOPIXEL_BRIGHTNESS` | `32` | Durum LED'i parlaklığı (0-255) |

## Yarışma günü: kanal planı

- Varsayılanlarımız **1, 6, 11** (2.4 GHz'te birbirini ezmeyen klasik
  üçlü) — otomatik kanal seçimi bu üçünden en boşunu alır. Zorunlu
  değil: `PROBOT_WIFI_AP_CHANNEL` ile 1-13 arası her kanal verilebilir.
  Aynı anda çalışan robotlara farklı ve sabit kanallar vermek
  (deterministik atama) koordineli bir filoda en güvenli yöntemdir.
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
   void teleopInit() { ledcAttachChannel(SERVO_PIN, 50, 14, 7); } // 50 Hz, 14-bit, kanal 7
   void teleopLoop() {
     uint16_t us = 500 + (angle/180.0f)*2000;        // 0-180° -> 500-2500 µs
     ledcWrite(SERVO_PIN, (uint32_t)us * 16383 / 20000);
   }
   void teleopStop() { ledcWrite(SERVO_PIN, 0); }    // darbeyi kes
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
| Mavi sabit | STOPPED, DS bağlı değil (INIT/RUN'da DS olmasa da faz rengi gösterilir) |
| Mavi yanıp sönüyor | DS bağlı + STOPPED |
| Sarı sabit | INIT (Auto, TeleOp veya Utility), Start bekleniyor |
| Sarı yanıp sönüyor | TRANSITION: Auto bitti, TeleOp önseçili; Init bekleniyor |
| Turuncu yanıp sönüyor | AUTO_RUN |
| Yeşil yanıp sönüyor | TELEOP_RUN |
| Mor yanıp sönüyor | UTILITY_RUN (kurulum ve bakım modu, bkz. API.md) |
| Kırmızı yanıp sönüyor | Stalled — loop 2 sn'den uzun döndü, güvende tutuluyor |
| Kırmızı sabit | Acil durdurma (kilitli, reboot gerekli) |

**RSL (sinyal lambası):** `#define PROBOT_RSL_PIN <gpio>` verirseniz kütüphane
o digital pini yalnız AUTO_RUN/TELEOP_RUN/UTILITY_RUN'da yanıp söndürür; INIT, STOPPED,
TRANSITION ve E-stop'ta **sabit açık** tutar.

## Batarya ölçümü

Arayüzdeki batarya göstergesini beslemenin üç yolu var (hiçbiri açık değilse
gösterge "Veri yok" der):

**1) Gerilim bölücü + ADC** — iki dirençle en ucuz çözüm:

```
BAT+ ──[100k]──┬──[22k]── GND
               │
             GPIO5 (ADC1) ── 100nF ── GND
```

```cpp
#define PROBOT_BATTERY_ADC_PIN  5     // GPIO1-10 arası ŞART (ADC1)
#define PROBOT_BATTERY_R_TOP_K  100
#define PROBOT_BATTERY_R_BOT_K  22
```

- Pin **GPIO1-10** arasından seçilmeli: ADC2 pinleri (GPIO11-20) WiFi
  açıkken çalışmaz — yanlış pin derlemede [PB-E104] ile yakalanır.
- 100k/22k, 3S LiPo'nun 12.6 V tepesini 2.27 V'a indirir (ADC'nin doğrusal
  bölgesi). Orta uca 100nF kondansatör koyun; bölücüyü **ana anahtarın
  sonrasına** bağlayın ki robot kapalıyken pili süzmesin (~0.1 mA).
- Okuma eFuse kalibrasyonlu, 8 örnek ortalama + yumuşatma ile ~%1-2
  doğruluktadır; multimetreyle fark görürseniz `PROBOT_BATTERY_TRIM` ile
  ince ayar yapın.

**2) INA219 / INA226 I2C sensörü** — daha hassas, lehim istemez:

```cpp
#define PROBOT_BATTERY_INA       226   // ya da 219
// opsiyonel: PROBOT_BATTERY_INA_ADDR / _SDA / _SCL
```

Sensöre ulaşılamazsa çalışma zamanında [PB-E306] uyarısı düşer ve gösterge
"Veri yok"a döner. INA'nın shunt'ı üzerinden anlık akım da okunur:
`probot::io::battery::currentAmps()` (akım sensörü yoksa `NaN`). Düşük pil
uyarısı için `probot::io::battery::setLowVoltage(volt)` eşiği verip
`isLowVoltage()` okunur (varsayılan kapalı).

> **INA'nın I2C bus'ını paylaşmak:** kütüphane `Wire`'ı
> `PROBOT_BATTERY_INA_SDA` / `_SCL` (varsayılan 1 / 2) pinlerinde 100 kHz ve
> 20 ms işlem zaman aşımıyla başlatır, INA'yı kendi görevinden okur. Aynı
> pinlerdeki başka bir cihaz (IMU, OLED...) robot kodundan `Wire`'ı
> kullanabilir: bus aktarımları sıraya girer. Alınan baytlar girmez:
> `Wire.read()` onları ortak bir tampondan okur, ikisi aynı anda okursa
> nadiren iki cihazdan birinin okuması yanlış çıkar. Her okuması önemli
> olan cihazı `Wire1`'e (başka pinler) koyun. `Wire`'ı aynı pinlerle
> (`Wire.begin(1, 2)`) başlatın ya da hiç başlatmayın: ilk çalışan
> `Wire.begin()` pinleri belirler, başka pinler INA'yı ulaşılamaz yapar
> [PB-E306]. Robot kodundaki `Wire.setClock()` ve `Wire.setTimeOut()` INA'ya
> da uygulanır.
>
> **Shunt notu:** akım hesabı `PROBOT_BATTERY_INA_SHUNT_MOHM` (default 100)
> ile yapılır. INA226'nın ±81.92 mV shunt aralığı 100 mΩ ile ±0.82 A'da
> doyar — modülünüzdeki shunt değerini (örn. 2 mΩ) tanımlayın. Gerilim
> ölçümü shunt'tan bağımsızdır.

**3) Elle besleme** — kendi ölçümünüz varsa: `probot::setBatteryVoltage(v)`.

Gösterge (Dashboard) son ~8 saniyenin ortalamasını gösterir; **Logs →
History** grafiği örnekleri ortalamasız çizer — motor yükünde gerilim
çöküşünü (sag) oradan izleyin.

## Bağlantı davranışı (güvenlik)

- Joystick verisi **500 ms** kesilirse eksen/butonlar otomatik sıfırlanır
  → motorlar son komutla kaçmaz.
- DS **10 sn** tamamen sessiz kalırsa robot STOP'a geçer
  (`PROBOT_DS_TIMEOUT_FORCE_STOP 0` ile yumuşak moda alınabilir).
- Aynı anda **tek client** kontrol edebilir (ilk bağlanan IP sahip olur).
  İkinci cihaz arayüzü açarsa `403` alır. `/health` ve `/info` ise
  sahiplik gerektirmez — hakem/izleme cihazları serbestçe okuyabilir.

## FTC OpMode yaşam döngüsü

- Model FTC'deki OpMode akışıyla aynıdır: **Autonomous / TeleOp seç → INIT →
  START → STOP**. Mod yalnız STOPPED/TRANSITION'da değiştirilebilir.
- INIT'te ilgili `init()` bir kez, RUN'da `loop()` sürekli, INIT veya RUN'dan
  her çıkışta ilgili `stop()` bir kez çağrılır.
- Auto süresi `autonomousStart()` sonrasında başlar. Süre bitince
  `autonomousStop()` çalışır, TeleOp önseçilir ve sistem TRANSITION'da yeni
  bir INIT bekler; TeleOp otomatik başlamaz.
- Tüm hook'lar **tek kalıcı task'ta**, yalnız döngü sınırlarında çalışır.
  Stop/faz değişimi kullanıcı kodunu iş ortasında **kesmez** — bu yüzden
  bir Wire/I2C ya da malloc kilidi asla orphan olmaz (eski sürümlerdeki
  donmanın kök sebebi buydu).
- **Kural:** her `teleopLoop`/`autonomousLoop` turu bir gün **dönmeli**
  (öneri < ~2 sn). Blocking serbest, *sonsuz* blocking yasak. I2C/sensör
  çağrılarına timeout koyun — örn. `Wire.begin()` sonrası
  `Wire.setTimeOut(50);` — yoksa takılı bir cihaz turu kilitler.
- **Stop kooperatiftir:** o anki tur dönünce aktif OpMode `stop()`u koşar (en fazla
  bir loop periyodu gecikme). Anında kesme için acil durdurma kullanın.
- **Stall (halt-safe):** bir tur `PROBOT_LOOP_DEADLINE_MS` (2 sn) içinde
  dönmezse input sıfırlanır, LED kırmızı yanar, robot güvende tutulur —
  **task öldürülmez, çip reboot edilmez** (homing/relative state korunur).
  Tek seferlik `init`, `start` ve `stop` hook'larının süresi daha uzundur:
  `PROBOT_HOOK_DEADLINE_MS` (10 sn); `init`'te sensör kalibrasyonu sorun olmaz.

### İleri seviye: initLoop ve start

`autonomousInitLoop()` / `teleopInitLoop()` INIT ile START arasında robot
kımıldamadan tekrar çağrılır; input nötr ve RSL sabit kalır. Kamera ile saha
randomizasyonu okumak, gyro/sensör kalibrasyon durumunu yayınlamak veya maç
öncesi kontrol yapmak içindir. Stall deadline'ı burada da geçerlidir.

`autonomousStart()` / `teleopStart()` START anındaki bir kerelik zaman damgası
ve state reset işleri içindir. Çoğu robotta loop'un ilk turu yeterlidir; bu
yüzden ana örneklerde bu ileri seviye hook'lar kullanılmaz.

## Acil durdurma

- Arayüzdeki kırmızı **EMERGENCY STOP** butonu (ya da
  `/robotControl?cmd=estop`) kullanıcı task'ını öldürür, INIT/RUN'daki aktif
  OpMode `stop()`unu taze task'ta watchdog'lu çalıştırır ve robotu **reboot'a
  kadar kilitler** (STOPPED/TRANSITION'da hook yok; Init/Start
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
ESP32 için "probot" kütüphanesiyle (0.4.0) Arduino kodu yaz.
Önce API referansını oku:
https://raw.githubusercontent.com/probot-studio/probot-core/stable/API.md
Kurallar:
- setup()/loop() TANIMLAMA. autonomousInit/Loop/Stop ve
  teleopInit/Loop/Stop üçlülerini kullan; iki loop ve iki stop zorunlu.
- Joystick: auto js = probot::io::joystick_api::makeDefault();
  js.getLeftY() vb. (-1..+1). probot::io::gamepad() üzerinde getLeftX gibi
  metodlar YOKTUR. İkinci kumanda: makeForPort(1) (port 0-5).
- Servo için ham LEDC kullan: ilgili init'te ledcAttachChannel(pin,50,14,7)
  (yüksek kanal → motor analogWrite'ıyla çakışmaz), teleopLoop'ta ledcWrite.
- teleopLoop ~50 Hz çağrılır; içinde sonsuz döngü/uzun blocking/delay() yapma.
- Kalıcı uyarı/hata: probot::console::warning(...) / error(...) (DS Logs
  sayfası); probot::printf her turdaki panel içindir.
```

Makine-okur özet: [`llms.txt`](llms.txt) · Tam referans: [`API.md`](API.md)

## Sık sorunlar

| Belirti | Çözüm |
|---|---|
| "Sketch too big" | Partition Scheme → Huge APP (3MB No OTA) |
| `#error ... [PB-E101/E02]` | Makroları `#include <probot.h>`'den önce yazın — [detay](https://probotstudio.com/docs/hatalar/#pb-e101) |
| `redefinition of 'void robotInit()'` [PB-E106] | 0.3.x hook'ları kaldırıldı: kodu `teleopInit()`/`autonomousInit()` ve `teleopStop()`/`autonomousStop()`'a taşıyın; [detay](https://probotstudio.com/docs/hatalar/#pb-e106) |
| Arayüz açılmıyor / 403 | Başka bir cihaz bağlı (tek client kuralı). Diğerini kapatın, ~5 sn bekleyin |
| Joystick görünmüyor | Kumandada herhangi bir tuşa basın (tarayıcı gamepad'i tuşa basılınca tanır) |
| Kumandam yok | Joystick sekmesi → **Klavye** (bilgisayarda) ya da **Dokunmatik** (telefonda) kaynağını elle etkinleştirin — otomatik seçilmez |
| Sık kopma | Kanal çakışması — robotlara **farklı sabit kanallar** verin (varsayılanlar: 1/6/11) |
| `undefined reference to teleopLoop` | Zorunlu hook eksik [PB-E201] — dördünü de tanımlayın (boş olabilir) |
| Kırmızı yanıp sönen LED, robot tepkisiz | Deadline miss [PB-E301] — loop 2 sn dönmedi; [detay](https://probotstudio.com/docs/hatalar/#pb-e301) |
| Servo titriyor | Yukarıdaki "Servo kullanımı" bölümü |

## Destek ve lisans

- Hata bildirimi: https://github.com/probot-studio/probot-core/issues
- WhatsApp: +90 538 040 81 48
- Katkı: [CONTRIBUTING.md](CONTRIBUTING.md) · Davranış kuralları:
  [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)
- Lisans: MIT + Commons Clause ([LICENSE](LICENSE) ·
  [LICENSE-commercial](LICENSE-commercial)) — eğitim ve yarışma kullanımı
  ücretsiz, ticari lisans için tunagul54@gmail.com

---

# Probot (EN)

ESP32 communication library for educational robotics competitions:
the robot hosts a WiFi AP and a browser-based driver station; joystick
input streams over a binary WebSocket at 50 Hz with automatic failsafes
(input zeroing after 500 ms, robot stop after 10 s of DS silence).

**Install:** Arduino IDE Library Manager → "probot", or clone
https://github.com/probot-studio/probot-core into `~/Arduino/libraries/`.
Requires arduino-esp32 core 3.x and the **Huge APP (3MB No OTA)**
partition scheme. Board settings for the recommended Boardoza Pulse
S32-S3 (USB CDC On Boot Enabled and more): see README.md, "Board settings".

**Minimal sketch:** see the Turkish quick start above — the code is
identical. Define the three `PROBOT_WIFI_*` macros, include `probot.h`,
implement the FTC-style `autonomousInit/Loop/Stop` and
`teleopInit/Loop/Stop` hooks (both loop and stop hooks are required), and
read input via `probot::io::joystick_api::makeDefault()`. Do not define
`setup()`/`loop()` — the library owns them.

Docs site: [probotstudio.com/docs](https://probotstudio.com/docs) · Full API
reference: [API.md](API.md) · Machine-readable index:
[llms.txt](llms.txt) · Changes: [CHANGELOG.md](CHANGELOG.md) ·
Contributing: [CONTRIBUTING.md](CONTRIBUTING.md) · License: MIT +
Commons Clause ([LICENSE](LICENSE) · [LICENSE-commercial](LICENSE-commercial))
