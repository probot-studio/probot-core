# Probot API Referansı (0.3.0)

Tek sayfalık tam referans. Kurulum ve örnekler için: [README.md](README.md)

## Program iskeleti

`setup()` ve `loop()` kütüphaneye aittir — sketch'te **tanımlanmaz**.
Sketch şu altı fonksiyonu tanımlamak **zorundadır** (boş olabilirler):

```cpp
void robotInit();        // Arayüzde Init'e basılınca 1 kez
void robotEnd();         // Stop'ta 1 kez — motorları güvenli konuma al
void teleopInit();       // Teleop fazı başlarken 1 kez
void teleopLoop();       // Teleop boyunca ~50 Hz tekrar çağrılır
void autonomousInit();   // Otonom fazı başlarken 1 kez
void autonomousLoop();   // Otonom boyunca ~50 Hz tekrar çağrılır
```

Faz akışı (arayüzdeki tek buton yönetir):

```
STOP ──Init──▶ INITED ──Start──▶ [AUTONOMOUS (N sn)] ──▶ TELEOP ──Stop──▶ STOP
                                  (kapatılabilir)
```

- Otonom süresi ve aç/kapa arayüzden seçilir; süre bitince teleop'a
  kendiliğinden geçilir.
- **Altı hook, tek kalıcı task'ta** çalışır (**core 1**); boot'ta açılır,
  normal işleyişte **asla öldürülmez**. WiFi/sunucu core 0'dadır.
- Geçişler **kooperatiftir**: buton "istenen mod"u set eder, task geçişi
  o anki tur **bittikten sonra**, güvenli sınırda yapar. Bu yüzden bir
  Stop/faz değişimi kullanıcı kodunu iş ortasında (Wire/malloc kilidi
  tutarken) **kesemez** — eski sürümlerdeki orphaned-lock donmasının
  (issue #21) kök sebebi buydu.
- `Stop`: o anki loop turu dönünce `robotEnd()` çağrılır (en fazla bir
  loop periyodu gecikme; loop bloklarsa daha uzun). Anında kesme için
  **acil durdurma** ya da donanım E-stop kullanın.
- **Sözleşme:** her loop turu bir gün **dönmeli** (öneri < ~2 sn).
  Blocking serbest, *sonsuz* blocking yasak — I2C/sensör çağrılarına
  timeout koyun (`Wire.setTimeOut(50)`).
- **Stall (halt-safe):** bir loop turu `PROBOT_LOOP_DEADLINE_MS` (2000)
  içinde dönmezse input sıfırlanır, LED kırmızı yanıp söner, robot
  güvende tutulur — **task öldürülmez, çip reboot edilmez** (homing
  state'i korunur). Tur dönünce temizlenir.
- **Acil durdurma (`cmd=estop`):** kullanıcı task'ı öldürülür, `robotEnd()`
  watchdog'lu çalıştırılır, robot **reboot'a kadar kilitlenir**
  (init/start reddedilir). Bkz. HTTP tablosu + "Acil durdurma".

## Otonom rutin seçici (AutoChooser)

Birden fazla firmware'e derlenmiş otonom arasından Driver Station'tan seçim
yapın. Katalog boot sırasında kaydedilir; DS açıldığında rutinler görünür ve
seçim Init öncesinde veya sonrasında, Start öncesinde yapılabilir. Init mevcut
seçimi korur; Start seçilen rutini çalıştırır. Aynı firmware'de
seçenek değiştirmek için yeniden yükleme gerekmez; yeni kod içeren bir rutin
eklemek için firmware yeniden yüklenmelidir.

```cpp
void leftInit() { /* sol başlangıç hazırlığı */ }
void leftPeriodic() { /* ~50 Hz: sol başlangıç hareketleri */ }
void rightInit() { /* sağ başlangıç hazırlığı */ }
void rightPeriodic() { /* ~50 Hz: sağ başlangıç hareketleri */ }

void autonomousConfigure() {
  probot::autonomous::clearRoutines();
  probot::autonomous::addRoutine("LeftAuto", leftInit, leftPeriodic, true);
  probot::autonomous::addRoutine("RightAuto", rightInit, rightPeriodic);
}
```

- `autonomousConfigure()` opsiyonel boot hook'udur; katalog burada bir kez
  kaydedilir. `addRoutine(name, ...)` adı hem kimlik hem DS'te görünen ad yapar.
  Farklı kimlik/görünen ad gerekiyorsa `addRoutine(id, label, ...)` biçimi
  korunur.
- Adlar benzersiz, en fazla 15 karakter ve ASCII harf, rakam, `-`, `_`
  içermelidir. Ayrı görünen ad UTF-8 olabilir ve en fazla 31 bayttır. En fazla
  8 rutin kaydedilebilir.
- Son parametresi `true` olan tek rutin varsayılan olur; birden fazla
  varsayılan kaydı reddedilir. Varsayılan yoksa DS seçim yapılana kadar
  otonom Start'ı engeller.
- Duplicate/bozuk kimlik, callback eksikliği, birden fazla varsayılan veya
  kapasite taşması katalog hata bayrağını açar. DS hatayı gösterir ve robot
  otonom başlatmayı reddeder; teleop kullanılabilir.
- Katalog boot boyunca sabit kalır. Aynı firmware içindeki seçim DS'ten
  yapılır; seçim çalışan otonom sırasında değiştirilemez. `robotInit()` normal
  robot donanım başlangıcı için kullanılmaya devam eder.
- Katalog boşsa `autonomousInit()` ve `autonomousLoop()` önceki sketch'lerdeki
  gibi çalışır. Katalog doluysa kütüphane seçili rutinin iki callback'ini
  çağırır; altı lifecycle hook'u yine sketch'te tanımlanmalıdır.
- Örnek: `examples/AutonomousChooser`.

## Joystick

```cpp
#include <probot.h>   // joystick_api dahildir

auto js = probot::io::joystick_api::makeDefault();
```

`makeDefault()` her çağrıda hafif bir sarmalayıcı döndürür; loop içinde
her seferinde çağırmak normaldir.

| Metod | Dönüş | Açıklama |
|---|---|---|
| `getLeftX()`, `getLeftY()` | `float` -1..+1 | Sol çubuk. Y yukarı = pozitif. Deadzone 0.08 |
| `getRightX()`, `getRightY()` | `float` -1..+1 | Sağ çubuk |
| `getLeftTriggerAxis()`, `getRightTriggerAxis()` | `float` 0 / 1 | Tetikler (buton olarak okunur) |
| `getA()`, `getB()`, `getX()`, `getY()` | `bool` | Xbox isimleri |
| `getCross()`, `getCircle()`, `getSquare()`, `getTriangle()` | `bool` | PlayStation eşdeğerleri |
| `getLB()`, `getRB()` | `bool` | Omuz butonları |
| `getBack()`, `getStart()`, `getOptions()` | `bool` | Orta butonlar (`getOptions` yalnız `tuna-default` eşlemesinde tanımlı) |
| `getLeftStickButton()`, `getRightStickButton()` | `bool` | Çubuğa basma (L3/R3) |
| `getPOV()` | `int` | D-Pad: -1 yok, 0 yukarı, 90 sağ, 180 aşağı, 270 sol |
| `getDpadUp()/Right()/Down()/Left()` | `bool` | D-Pad tek yön |
| `getRawAxis(i)`, `getRawButton(i)` | `float` / `bool` | Ham erişim (mapping'siz) |
| `isConnected()` | `bool` | En az bir eksen/buton verisi geldi mi |
| `getSeq()`, `getMs()` | `uint32_t` | Paket sayacı / son paket zamanı |
| `getAxisCount()`, `getButtonCount()` | `uint32_t` | Kumandanın bildirdiği sayılar |

**Failsafe:** joystick verisi 500 ms kesilirse tüm eksen/butonlar
sıfır okunur. Ayrıca bağlantı koptuğunda state anında sıfırlanır.
Motor kodunu doğrudan eksen değerine bağlamak güvenlidir.

**Deadzone/ayarlar:**

```cpp
probot::io::joystick_api::Options opt;
opt.deadzone = 0.12f;          // varsayılan 0.08
auto js = probot::io::joystick_api::makeDefault(opt);
```

**Kumanda eşlemesi (mapping):** varsayılan `logitech-f310` (W3C
standart düzenle aynı). Farklı kumanda için:

```cpp
probot::io::joystick_mapping::setActiveByName("standard"); // robotInit içinde
// isimler: "logitech-f310"/"f310", "standard"/"xbox"/"ds4", "axis9-dpad", "tuna-default"
```

## Telemetri

Driver Station arayüzündeki panele yazar (256 baytlık halka tampon —
eskiyen satırlar düşer):

```cpp
probot::print("merhaba");
probot::println("satır");
probot::printf("hiz=%.2f\n", hiz);
probot::clearTelemetry();
```

## Servo (kütüphane sınıf SAĞLAMAZ — kalıp)

probot çıkış donanımını sarmalamaz; servoyu **ham LEDC** ile sen sürersin.
Servo 50 Hz / 14-bit ister; `analogWrite` (~1 kHz, motorlar) uymaz. Titreme
(timer çakışması) olmaması için servoya **yüksek bir LEDC kanalı** ver —
motorlar `analogWrite` ile alttan (0,1,2…) kullanır, çakışmaz:

```cpp
#define SERVO_PIN 4
void robotInit(){ ledcAttachChannel(SERVO_PIN, 50, 14, 7); }  // 50 Hz, 14-bit, kanal 7
void teleopLoop(){
  uint16_t us = 500 + (angle/180.0f)*2000;          // 0-180° -> 500-2500 µs
  ledcWrite(SERVO_PIN, (uint32_t)us * 16383 / 20000);
}
void robotEnd(){ ledcWrite(SERVO_PIN, 0); }          // darbeyi kes (güvenli)
```

Tam çalışan örnek: `examples/ServoTest`. Güç uyarıları: README "Servo
kullanımı" (servoyu ayrı 5-6 V kaynaktan besle, GND ortak).

`attach()` ilk `write()`'a kadar darbe üretmez — robot açılışta zıplamaz.

## Durum LED'i (NeoPixel) + RSL

Builtin NeoPixel **yalnız maç durumunu** gösterir; renkleri kütüphane sürer
(tablo README'de). **El ile renk atama API'si yoktur** — LED'in rengi her zaman
bir anlam taşır. Pin varsayılanı GPIO 3 (`#define NEOPIXEL_PIN 48` ile değişir);
parlaklık `#define NEOPIXEL_BRIGHTNESS 32`.

Ek bir sinyal lambası (FRC RSL tarzı) için düz bir digital pin verin:

```cpp
#define PROBOT_RSL_PIN 10   // probot.h'den önce
```

Kütüphane bu pini sürer: robot **hareket edebilirken** (teleop/otonom) yanıp
söner, aksi halde (disabled/stop/e-stop) **sabit açık** kalır.

## Robot durumu (ileri seviye)

```cpp
auto s = probot::robot::state().read();   // atomik snapshot
// s.status : Status::INIT/START/STOP
// s.phase  : Phase::NOT_INIT/INITED/AUTONOMOUS/TELEOP
// s.autonomousEnabled, s.autoPeriodSeconds, s.autoStartMs
// s.clientCount, s.deadlineMiss, s.batteryVoltage
```

## Yapılandırma makroları

Tamamı `#include <probot.h>`'den **önce** tanımlanır. Tablo:
README "Ayar makroları". Zorunlu olanlar: `PROBOT_WIFI_AP_PASSWORD`
(≥8 karakter) ve `PROBOT_WIFI_AP_CHANNEL` (1-13). Açılışta otomatik
kanal seçimi opt-in'dir: `PROBOT_WIFI_AUTO_CHANNEL 1` (varsayılan
kapalı, filoda önerilmez — bkz. README kanal planı).

Yaşam döngüsü / güvenlik makroları (0.3.0):

| Makro | Varsayılan | Anlamı |
|---|---|---|
| `PROBOT_LOOP_DEADLINE_MS` | 2000 | Bir loop turu bu süreyi aşarsa "stalled" — input sıfır, halt-safe |
| `PROBOT_WDT_TIMEOUT_S` | 8 | Donanım watchdog (yalnız sysloop abone; > loop deadline olmalı) |
| `PROBOT_ESTOP_END_MS` | 500 | Acil durdurmada `robotEnd()`'e tanınan süre; aşılırsa reboot |
| `PROBOT_ESTOP_ENABLE_PIN` | -1 | Kütüphanenin sürdüğü enable GPIO'su (-1 = kapalı). Boot'ta HIGH, estop'ta LOW |
| `USER_LOOP_PERIOD_MS` | 20 | Loop çağrı periyodu (~50 Hz) |

## Acil durdurma

İki ayrı durdurma var:

- **Stop** (`cmd=stop`): kooperatif. O anki tur dönünce `robotEnd()` koşar.
  Robot tekrar Init/Start edilebilir.
- **Emergency stop** (`cmd=estop`): terminal. Sırası: latch → enable pini
  LOW → kullanıcı task'ını öldür → taze task'ta `robotEnd()`'i
  `PROBOT_ESTOP_END_MS` watchdog'lu çalıştır (takılırsa `ESP.restart()`).
  Robot **reboot'a kadar kilitli** — `init`/`start` reddedilir (`cmd=reboot`
  ya da güç döngüsü temizler). Donmuş bir loop'u bile durdurur (task
  öldürülür); ama gerçek güvenlik garantisi için **donanım E-stop**'u güç/
  enable hattına koyun — çip tamamen kilitliyse yalnız o çalışır.

## HTTP / WebSocket arayüzü

Robot `192.168.4.1:80`'de tek sunucu çalıştırır. Kendi DS istemcinizi
yazacaksanız:

| Endpoint | Metod | Sahiplik | Açıklama |
|---|---|---|---|
| `/` | GET | gerekli | Driver Station arayüzü (SPA) |
| `/joystick` | WS | gerekli | Çift yönlü binary kanal (çerçeve formatları aşağıda) |
| `/updateController` | POST | gerekli | JSON fallback: `{"axes":[...],"buttons":[...]}` — WS koptuğunda |
| `/robotControl?cmd=init\|start\|stop\|cancelAuto&auto=0\|1&autoLen=N` | GET | gerekli | Faz komutları. AutoChooser varsa `start` ayrıca doğrulanmış `autoId` ister |
| `/robotControl?cmd=selectAuto&autoId=ID` | GET | gerekli | Yalnız robot çalışmıyorken kayıtlı otonom seçer; bilinmeyen ID 400, aktif faz 409 |
| `/getAutoModes` | GET | gerekli | Kayıtlı rutin kimlikleri/adları, varsayılan ve seçili rutin |
| `/robotControl?cmd=estop` | GET | gerekli | **Acil durdurma**: kullanıcı task'ı öldürülür, `robotEnd()` watchdog'lu (`PROBOT_ESTOP_END_MS`) çalışır, enable pini kesilir, robot reboot'a kadar kilitlenir |
| `/robotControl?cmd=reboot` | GET | gerekli | Çipi yeniden başlatır (`ESP.restart()`) — estop kilidini temizlemenin yolu |
| `/setChannel?ch=N` | GET | gerekli | Kanalı NVS'e kaydet; 1-13 ise CSA ile **canlı** geçiş (zaten o kanaldaysa `live:false`), `0` = kaydı temizle, açılışta firmware varsayılanına dön. Dönüş: `{"ok":b,"ch":N,"live":b}` |
| `/getState` | GET | gerekli | Faz, otonom sayacı, E-stop ve varsa `autoChooser` / `selectedAutoId` (WS yokken fallback) |
| `/telemetry` | GET | gerekli | Telemetri tamponunun içeriği (text) (WS yokken fallback) |
| `/getBattery` | GET | serbest | Pil gerilimi (şu an kullanıcı beslemeli) |
| `/health` | GET | serbest | `{"rssi":N,"up":ms,"heap":N,"dm":b,"joyAgeMs":N,"sta":N,"disc":N}` — izleme/hakem için. `joyAgeMs`: son joystick paketinin yaşı (-1 = hiç gelmedi), `sta`: bağlı istemci sayısı, `disc`: son kopuşun IEEE reason kodu |
| `/info` | GET | serbest | SSID, kanal + `chSource` (macro/nvs/auto), IP, çip/heap/flash |
| `/portal` | GET | serbest | Captive portal karşılama sayfası (`PROBOT_CAPTIVE_PORTAL 0` ile kapatılır) |

**Captive portal:** robot, AP'sine katılan cihazların DNS sorgularını
kendine çözer ve işletim sistemi bağlantı sondalarını (`/generate_204`,
`/hotspot-detect.html`, `/connecttest.txt` vb.) yakalar — tablete
bağlanınca karşılama sayfası kendiliğinden açılır, IP yazmak gerekmez.

**Sahiplik (owner) modeli:** korumalı endpoint'e ilk istek atan IP
sahip olur; diğer IP'ler `403 Forbidden` alır. Sahip
`PROBOT_DS_OWNER_TIMEOUT_MS` (5 sn) sessiz kalırsa slot boşalır.
Sahip düştüğünde gamepad verisi anında sıfırlanır.

**WS çerçeveleri** — ilk bayt tipi belirler.

İstemci → robot:

```
'J' 0x4A  joystick verisi:
  [1]      uint8   eksen sayısı   (maks 20)
  [2]      uint8   buton sayısı   (maks 20)
  [3]      uint8   rezerve (0)
  [4..]    int16   eksenler, big-endian, değer = float × 32767
  [sonra]  uint8[] butonlar, bit-paketli, LSB önce
'P' 0x50  boşta keepalive (tek bayt) — gamepad yokken 2 sn'de bir;
          owner slotunu ve DS aktivitesini canlı tutar
```

Robot → istemci (push):

```
'S' 0x53  durum+sağlık JSON'u — değişiklikte bir sonraki tick'te
          (250 ms), değişiklik yoksa en geç ~1.25 sn'de bir (heartbeat
          görevi de görür). Alanlar /getState + /health birleşimi
          (`estop` alanı dahil: acil durdurma kilidi).
'T' 0x54  telemetri tamponu (text) — içerik değiştiğinde
```

≥5 saniye hiç çerçeve alamayan istemci bağlantıyı ölü sayıp yeniden
bağlanmalıdır.

**Bağlantı kesilme zinciri:**

1. Joystick verisi 500 ms kesilir → eksenler sıfır okunur.
2. Sahip 5 sn istek atmaz → owner slotu boşalır, gamepad sıfırlanır.
3. DS 10 sn tamamen sessiz → `PROBOT_DS_TIMEOUT_FORCE_STOP=1` (varsayılan)
   ise robot STOP'a geçer; `0` ise loop'lar sürer, bağlantı dönünce
   kaldığı yerden devam eder.

## Derleme hedefleri

- Arduino IDE / arduino-cli: `library.properties` ile (`make build EXAMPLE=JoystickTest`)
- PlatformIO: `lib_deps = https://github.com/probot-studio/probot-core.git`
- ESP-IDF + Arduino component: `probot::runtime_setup()` çağırın
- Host unit testleri (donanımsız, g++ ile):
  `make tests/control_tests && ./tests/control_tests`
