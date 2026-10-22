# Probot API Referansı (0.4.0)

Tek sayfalık tam referans. Kurulum ve örnekler için: [README.md](README.md)

## Program iskeleti

Yaşam döngüsü FTC'deki OpMode modelinin aynısıdır. `setup()` ve Arduino
`loop()` kütüphaneye aittir; sketch'te **tanımlanmaz**. Normal kullanımda
her mod için `init/loop/stop` üçlüsü yazılır:

```cpp
void autonomousInit() {}
void autonomousLoop() {}
void autonomousStop() {}

void teleopInit() {}
void teleopLoop() {}
void teleopStop() {}
```

`autonomousLoop`, `autonomousStop`, `teleopLoop` ve `teleopStop` zorunludur;
boş da olsalar tanımlanmazlarsa linker hatası oluşur. `init` hook'ları
opsiyoneldir ama donanım hazırlığı için ana iskelette gösterilir.

| Hook | Çağrı zamanı | Sözleşme |
|---|---|---|
| `autonomousInit()` | Auto seçiliyken INIT, 1 kez | Opsiyonel (weak; tanımlanmazsa boş sayılır) |
| `autonomousInitLoop()` | Auto INIT'ten START'a kadar sürekli | Opsiyonel (weak; tanımlanmazsa boş sayılır) |
| `autonomousStart()` | Auto START anında, 1 kez | Opsiyonel (weak; tanımlanmazsa boş sayılır) |
| `autonomousLoop()` | Auto RUN boyunca, `USER_LOOP_PERIOD_MS` | **Zorunlu** |
| `autonomousStop()` | Auto INIT/RUN'dan her çıkışta, 1 kez | **Zorunlu** |
| `teleopInit()` | TeleOp seçiliyken INIT, 1 kez | Opsiyonel (weak; tanımlanmazsa boş sayılır) |
| `teleopInitLoop()` | TeleOp INIT'ten START'a kadar sürekli | Opsiyonel (weak; tanımlanmazsa boş sayılır) |
| `teleopStart()` | TeleOp START anında, 1 kez | Opsiyonel (weak; tanımlanmazsa boş sayılır) |
| `teleopLoop()` | TeleOp RUN boyunca, `USER_LOOP_PERIOD_MS` | **Zorunlu** |
| `teleopStop()` | TeleOp INIT/RUN'dan her çıkışta, 1 kez | **Zorunlu** |

Akış:

```
STOPPED[mode] ──INIT──▶ INIT[mode] ──START──▶ RUN[mode] ──STOP──▶ STOPPED
                           │                         │
                           └────────STOP────────────┘

RUN[AUTO] ──süre dolar──▶ autonomousStop() ──▶ TRANSITION[TELEOP]
TRANSITION ──INIT──▶ INIT[TELEOP]
```

- Mod yalnız STOPPED/TRANSITION'da seçilir. INIT/RUN sırasında `cmd=mode`
  409 döndürür; önce STOP gerekir.
- Otonom sayacı `autonomousStart()` döndükten sonra başlar. Süre bitince
  TeleOp **çalışmaya başlamaz**; yalnız önseçilir ve yeni bir INIT beklenir.
- **Tüm hook'lar tek kalıcı task'ta** çalışır (**core 1**); boot'ta açılır,
  normal işleyişte **asla öldürülmez**. WiFi/sunucu core 0'dadır.
- Geçişler **kooperatiftir**: buton "istenen mod"u set eder, task geçişi
  o anki tur **bittikten sonra**, güvenli sınırda yapar. Bu yüzden bir
  Stop/faz değişimi kullanıcı kodunu iş ortasında (Wire/malloc kilidi
  tutarken) **kesemez** — eski sürümlerdeki orphaned-lock donmasının
  (issue #21) kök sebebi buydu.
- `Stop`: o anki tur dönünce aktif modun `stop()` hook'u çağrılır (en fazla bir
  loop periyodu gecikme; loop bloklarsa daha uzun). Anında kesme için
  **acil durdurma** ya da donanım E-stop kullanın.
- **Sözleşme:** her loop turu bir gün **dönmeli** (öneri < ~2 sn).
  Blocking serbest, *sonsuz* blocking yasak — I2C/sensör çağrılarına
  timeout koyun (`Wire.setTimeOut(50)`).
- **Stall (halt-safe):** bir `initLoop`/`loop` turu `PROBOT_LOOP_DEADLINE_MS` (2000)
  içinde dönmezse input sıfırlanır, LED kırmızı yanıp söner, robot
  güvende tutulur — **task öldürülmez, çip reboot edilmez** (homing
  state'i korunur). Tur dönünce temizlenir. Tek seferlik `init`/`start`/`stop`
  hook'larının sınırı daha uzundur (`PROBOT_HOOK_DEADLINE_MS`, 10 sn); uzun
  kalibrasyon gibi işler `init()`'te yapılabilir.
- **Acil durdurma (`cmd=estop`):** kullanıcı task'ı öldürülür; INIT/RUN'daki
  aktif modun `stop()` hook'u taze task'ta watchdog'lu çalıştırılır. STOPPED/
  TRANSITION'da hook çağrılmaz. Robot **reboot'a kadar kilitlenir**
  (init/start reddedilir). Bkz. HTTP tablosu + "Acil durdurma".

### İleri seviye hook'lar

`autonomousInitLoop()` / `teleopInitLoop()`, INIT ile START arasında robot
kımıldamadan kod koşturur; bu evrede input nötr tutulur ve RSL sabittir.
Kamera ile saha randomizasyonu okumak, gyro/sensör kalibrasyon durumunu
yayınlamak ve maç öncesi kontrol yapmak için uygundur. Bu hook'lar da stall
deadline'ına tabidir.

`autonomousStart()` / `teleopStart()` START anındaki bir kerelik zaman damgası
veya state reset işi içindir. Çoğu robotta `loop()`un ilk turu yeterlidir.

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
probot::io::joystick_mapping::setActiveByName("standard"); // ilgili OpMode init içinde
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
void teleopInit(){ ledcAttachChannel(SERVO_PIN, 50, 14, 7); } // 50 Hz, 14-bit, kanal 7
void teleopLoop(){
  uint16_t us = 500 + (angle/180.0f)*2000;          // 0-180° -> 500-2500 µs
  ledcWrite(SERVO_PIN, (uint32_t)us * 16383 / 20000);
}
void teleopStop(){ ledcWrite(SERVO_PIN, 0); }        // darbeyi kes (güvenli)
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

Kütüphane bu pini yalnız AUTO_RUN/TELEOP_RUN'da yanıp söndürür; INIT,
STOPPED, TRANSITION ve E-stop'ta **sabit açık** tutar.

## Robot durumu (ileri seviye)

```cpp
auto s = probot::robot::state().read();   // atomik snapshot
// s.status : Status::INIT/START/STOP
// s.phase  : STOPPED/AUTO_INIT/AUTO_RUN/TELEOP_INIT/TELEOP_RUN/TRANSITION
// s.selectedMode : OpMode::AUTO/TELEOP
// s.autoPeriodSeconds, s.autoStartMs
// s.clientCount, s.deadlineMiss, s.batteryVoltage
```

Protokolde `Phase` sayıları sabittir:

| Değer | Faz |
|---:|---|
| 0 | `STOPPED` |
| 1 | `AUTO_INIT` |
| 2 | `AUTO_RUN` |
| 3 | `TELEOP_INIT` |
| 4 | `TELEOP_RUN` |
| 5 | `TRANSITION` |

`status`, son kabul edilen komut evresini koruyan sade alan olarak
`Status::INIT` (0), `Status::START` (1), `Status::STOP` (2) taşır. Gerçek,
hook'u tamamlanmış evre için daima `phase` kullanılmalıdır.

## Yapılandırma makroları

Tamamı `#include <probot.h>`'den **önce** tanımlanır. Tablo:
README "Ayar makroları". Zorunlu olanlar: `PROBOT_WIFI_AP_PASSWORD`
(≥8 karakter) ve `PROBOT_WIFI_AP_CHANNEL` (1-13). Açılışta otomatik
kanal seçimi opt-in'dir: `PROBOT_WIFI_AUTO_CHANNEL 1` (varsayılan
kapalı, filoda önerilmez — bkz. README kanal planı).

Yaşam döngüsü / güvenlik makroları:

| Makro | Varsayılan | Anlamı |
|---|---|---|
| `PROBOT_LOOP_DEADLINE_MS` | 2000 | Bir initLoop/loop turu bu süreyi aşarsa "stalled" — input sıfır, halt-safe |
| `PROBOT_HOOK_DEADLINE_MS` | 10000 | Tek seferlik init/start/stop hook'ları için aynı stall sınırı (loop deadline'ından küçük olamaz) |
| `PROBOT_WDT_TIMEOUT_S` | 8 | Donanım watchdog (yalnız sysloop abone; > loop deadline olmalı) |
| `PROBOT_ESTOP_END_MS` | 500 | Acil durdurmada aktif `stop()` hook'una tanınan süre; aşılırsa reboot |
| `PROBOT_ESTOP_ENABLE_PIN` | -1 | Kütüphanenin sürdüğü enable GPIO'su (-1 = kapalı). Boot'ta HIGH, estop'ta LOW |
| `USER_LOOP_PERIOD_MS` | 20 | Loop çağrı periyodu (~50 Hz) |

## Hata kodları (PB-Exx)

Kütüphanenin ürettiği her hata/uyarı kalıcı bir kod taşır; ayrıntılı çözüm
dokümanın **Hatalar** sayfasındadır: `probotstudio.com/docs/hatalar/#pb-eXXX`.
Bantlar: **E1xx** derleme · **E2xx** bağlama (linker) · **E3xx** çalışma
zamanı · **E4xx** protokol/HTTP (E409, HTTP 409'un karşılığıdır).

| Kod | Yüzey | Anlamı |
|---|---|---|
| PB-E101 | Derleme | `PROBOT_WIFI_AP_PASSWORD` eksik ya da 8 karakterden kısa |
| PB-E102 | Derleme | `PROBOT_WIFI_AP_CHANNEL` eksik ya da 1-13 dışında |
| PB-E103 | Derleme | SSID uzunluk kuralları (MAC ekiyle ≤25, eksiz ≤32) |
| PB-E104 | Derleme | Batarya ADC konfigürasyonu geçersiz — pin ADC1 dışında (S3'te GPIO1-10 şart; ADC2 WiFi açıkken çalışmaz) ya da bölücü dirençleri (`_R_TOP_K`/`_R_BOT_K`) eksik |
| PB-E105 | Derleme | Batarya kaynak çakışması/geçersiz seçim — hem `PROBOT_BATTERY_ADC_PIN` hem `PROBOT_BATTERY_INA` tanımlı; ya da `_INA` 219/226 değil; ya da `_TRIM` 0.5-2.0 dışında |
| PB-E106 | Derleme | `redefinition of void robotInit()` / `robotEnd()`: 0.3.x hook'ları 0.4.0'da kaldırıldı; kurulum kodunu `teleopInit()`/`autonomousInit()`'e, güvenli durdurmayı `teleopStop()`/`autonomousStop()`'a taşıyın |
| PB-E201 | Bağlama | Zorunlu hook tanımsız — `` undefined reference to `_Z10teleopLoopv' `` vb. (bağlayıcı adı karışık yazar) Dördü de (boş olsa bile) tanımlanmalı |
| PB-E202 | Derleme | `redefinition of 'void setup()'` (ya da `loop()`): `setup()`/`loop()` sketch'te tanımlanmış — kütüphaneye aittir, hook'ları kullanın |
| PB-E301 | Çalışma | Deadline miss / stall: bir `initLoop`/`loop` turu `PROBOT_LOOP_DEADLINE_MS`'i (ya da tek seferlik `init`/`start`/`stop` hook'u `PROBOT_HOOK_DEADLINE_MS`'i) aştı; girişler sıfır, halt-safe |
| PB-E302 | Çalışma | Emergency stop kilitli — reboot gerekli |
| PB-E303 | Çalışma | DS bağlantısı koptu → robot durduruldu (`PROBOT_DS_TIMEOUT_FORCE_STOP=1`) |
| PB-E304 | Çalışma | DS bağlantısı koptu → joystick nötr, yeniden bağlanma bekleniyor (`FORCE_STOP=0`) |
| PB-E305 | Çalışma | E-stop'ta `stop()` hook'u `PROBOT_ESTOP_END_MS` içinde dönmedi → çip reboot |
| PB-E306 | Çalışma | Batarya sensörüne (INA219/INA226) I2C'de ulaşılamıyor — arayüz "Veri yok"a düşer, bağlantı/adres kontrol edin |
| PB-E409 | HTTP | Komut geçersiz evrede (409) — `mode`/`init`/`start`/`stop` evre kuralları |

## Acil durdurma

İki ayrı durdurma var:

- **Stop** (`cmd=stop`): kooperatif. O anki tur dönünce aktif modun `stop()`u koşar.
  Robot tekrar Init/Start edilebilir.
- **Emergency stop** (`cmd=estop`): terminal. Sırası: latch → enable pini
  LOW → kullanıcı task'ını öldür → aktif mod varsa taze task'ta `stop()`unu
  `PROBOT_ESTOP_END_MS` watchdog'lu çalıştır (takılırsa `ESP.restart()`).
  Robot **reboot'a kadar kilitli** — `init`/`start` reddedilir (`cmd=reboot`
  ya da güç döngüsü temizler). Donmuş bir loop'u bile durdurur (task
  öldürülür); ama gerçek güvenlik garantisi için **donanım E-stop**'u güç/
  enable hattına koyun — çip tamamen kilitliyse yalnız o çalışır.

Koddan tetikleme:

```cpp
probot::emergencyStop();   // her task'tan güvenli, kullanıcı hook'u dahil
```

Sadece bir bayrak set eder; gerçek sırayı (task kill → watchdog'lu aktif
`stop()` → kilit) sysloop yürütür — bu yüzden çağıran task kendini
silmez. Arayüzdeki EMERGENCY STOP butonuyla aynı yola çıkar.

## HTTP / WebSocket arayüzü

Robot `192.168.4.1:80`'de tek sunucu çalıştırır. Kendi DS istemcinizi
yazacaksanız:

| Endpoint | Metod | Sahiplik | Açıklama |
|---|---|---|---|
| `/` | GET | gerekli | Driver Station arayüzü (SPA) |
| `/joystick` | WS | gerekli | Çift yönlü binary kanal (çerçeve formatları aşağıda) |
| `/updateController` | POST | gerekli | JSON fallback: `{"axes":[...],"buttons":[...]}` — WS koptuğunda |
| `/robotControl?cmd=mode&val=auto\|teleop` | GET | gerekli | STOPPED/TRANSITION'da OpMode seçer; INIT/RUN'da 409 |
| `/robotControl?cmd=init&autoLen=N` | GET | gerekli | Seçili modu INIT'e alır; yalnız STOPPED/TRANSITION |
| `/robotControl?cmd=start&autoLen=N` | GET | gerekli | Seçili modu RUN'a alır; yalnız ilgili INIT fazı |
| `/robotControl?cmd=stop` | GET | gerekli | INIT/RUN'dan (ya da kabul edilmiş ama henüz faza yansımamış bir INIT/START'tan) kooperatif stop; diğer durumlarda 409 |
| `/robotControl?cmd=estop` | GET | gerekli | **Acil durdurma**: kullanıcı task'ı öldürülür, aktif `stop()` watchdog'lu (`PROBOT_ESTOP_END_MS`) çalışır, enable pini kesilir, robot reboot'a kadar kilitlenir |
| `/robotControl?cmd=reboot` | GET | gerekli | Çipi yeniden başlatır (`ESP.restart()`) — estop kilidini temizlemenin yolu |
| `/setChannel?ch=N` | GET | gerekli | Kanalı NVS'e kaydet; 1-13 ise CSA ile **canlı** geçiş (zaten o kanaldaysa `live:false`), `0` = kaydı temizle, açılışta firmware varsayılanına dön. Dönüş: `{"ok":b,"ch":N,"live":b}` |
| `/getState` | GET | gerekli | `{"status":N,"phase":N,"selectedMode":"auto|teleop","autoPeriodSeconds":N,"autoRemainingMs":N,"batt":V.V,"estop":b}` (WS yokken fallback). `batt`: pil gerilimi (bölücü/INA otomatik ya da `setBatteryVoltage()` elle), `0.0` = veri yok |
| `/telemetry` | GET | gerekli | Telemetri tamponunun içeriği (text) (WS yokken fallback) |
| `/getBattery` | GET | serbest | Pil gerilimi (kaynak: `PROBOT_BATTERY_ADC_PIN` bölücü, `PROBOT_BATTERY_INA` sensörü ya da elle `probot::setBatteryVoltage()`) |
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
          (`estop` alanı dahil: acil durdurma kilidi; `batt` alanı
          dahil: pil gerilimi, 0.0 = veri yok).
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
