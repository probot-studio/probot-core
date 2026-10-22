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

## Tipli durum: `probot::ds` (ileri seviye)

`#include <probot/api.hpp>` ile her `.cpp` dosyasından kullanılabilir.

| Fonksiyon | Döner |
|---|---|
| `ds::phase()` | Çalışan faz; acil durdurma kilitlendiyse `Phase::STOPPED` |
| `ds::selectedMode()` | DS'te seçili mod (`OpMode::AUTO` / `TELEOP`) |
| `ds::outputsEnabled()` | Aktüatör sürülebilir: başlamış RUN, stall yok, acil durdurma yok |
| `ds::isEStopped()` | Acil durdurma kilitli (reboot'a kadar) |
| `ds::isAttached()` | Robotu sahiplenmiş bir DS istemcisi var |
| `ds::matchTimeSeconds()` | Otonom süresinden kalan saniye (AUTO_RUN'da); zamanlı dönem yoksa `-1` |
| `ds::packetSeq()` | Her DS joystick paketinde ve kabul edilen mod/INIT/START/STOP komutunda artar (acil durdurma ve reboot isteğinde artmaz) |
| `ds::rawGamepad(port, out)` | Kapısız son joystick karesi, her fazda (yalnız `port` 0). Kare yoksa `false` |

`probot::io::rawGamepad()` aynı kapısız kaynağın servisidir. Menü, otonom
seçimi ve buton eşlemesi içindir; motor **sürmeyin**: aktüatör için kapılı
`probot::io::gamepad()` / `joystick_api` kullanın. Kapısız veri de bağlantı
kesilince `PROBOT_INPUT_TIMEOUT_MS` sonra sıfırlanır.

## Framework katmanı (ileri seviye)

Kendi döngüsünü süren bir katman (ör. robot framework SDK'sı) user task'ta
her fazda çalışabilir:

```cpp
#include <probot/api.hpp>
static const bool kInstalled = probot::framework::install({
  &onBoot,          // user task'ta, ilk turdan önce bir kez
  &onPreCycle,      // her turda, TÜM fazlarda, OpMode hook'undan önce
  &onPostCycle,     // her turda, TÜM fazlarda, OpMode hook'undan sonra
  &onEmergencyStop  // acil durdurmada, user task silindikten sonra
});
```

- `install()` bir kez ve runtime başlamadan önce kabul edilir (statik
  ilklendirme uygundur); sonrası `false` döner. Her alan `nullptr` olabilir.
- `preCycle`/`postCycle` INIT ve RUN fazlarında OpMode hook'larıyla aynı
  stall sınırına (`PROBOT_LOOP_DEADLINE_MS`, PB-E301) tabidir. STOPPED ve
  TRANSITION'da stall denetimi yoktur; orada takılma algılanmaz.
- `emergencyStop` user task silindikten sonra core 1'de yeni bir task'ta,
  aktif `stop()` hook'undan önce çalışır. Stop hook'uyla birlikte
  `PROBOT_ESTOP_END_MS` içinde bitmezse çip reboot eder (PB-E305). Kısa
  olmalı, user kodunun tutmuş olabileceği kilitleri almamalı: çıkışları
  güvenli duruma alıp dönmelidir.
- Kaydı sketch'ten referans edin (ya da PlatformIO'da `lib_archive = no`):
  arşivlenmiş bir kütüphane nesnesine hiçbir şey referans vermiyorsa linker
  onu statik ilklendiricisiyle birlikte atabilir.
- Sketch `#include <probot.h>`'den önce `PROBOT_FRAMEWORK` tanımlarsa OpMode
  hook'larının hepsi isteğe bağlı olur (PB-E201 link hatası çıkmaz).
  Tanımlanan hook'lar framework turunun içinde çalışmaya devam eder. Bu
  modda yanlış yazılmış bir hook adı hata vermez, sadece çalışmaz.

## Dashboard topic deposu (ileri seviye)

Telemetri ve ayarlanabilir değerler (tunable) için adlı, tipli değerler.
`#include <probot/api.hpp>` (ya da `<probot/dashboard/dashboard.hpp>`).
DS sayfasının **Veri** sekmesi topic'leri `/` ile ayrılmış bir ağaçta
canlı gösterir. `TUNABLE | MUTABLE` topic'ler oradan değiştirilir (Enter
ya da odaktan çıkınca gönderilir; robot değeri geri yollayana dek
"bekliyor", reddedilirse sebebi görünür). Özellik JSON'undaki `min`,
`max`, `step`, `unit` editöre uygulanır. `struct:X` değerleri `structschema`
şemasıyla alanlarına ayrılır. Kardeş `.type` topic'i `Selectable` ise
`options` listesinden `selected` yazan bir seçim kutusu, `Command` ise
`running` değerini çeviren bir düğme gösterilir. `Alerts` klasöründeki
`errors`, `warnings`, `infos` (string[]) etkin uyarıları seviyesiyle
listeler; `PIDController` klasöründe kazançlar (`p`, `i`, `d`, `izone`,
`setpoint`) önce gelir ve `/Tunables/<ad>` altında, aynı adlı
`/Telemetry/<ad>` tablosundaki `measurement` ile `error` de canlı görünür. Metin kutularına boşluk
yalnız robot STOPPED iken yazılır; diğer fazlarda Space yine acil
durdurmadır. Protokol aşağıda ("WS çerçeveleri").

```cpp
using namespace probot::dashboard;
TopicId kp = publish("/Tunables/kP", ValueType::Double, "double",
                     "{\"min\":0}", TUNABLE | MUTABLE);
double v = 0.5;
set(kp, &v, sizeof(v));               // aynı değer tekrar gönderilmez
drainWrites([](const Write& w, void*) {
  // DS'ten gelen değer: w.id, w.vt, w.data[0..w.len)
}, nullptr);
```

| Fonksiyon | Anlamı |
|---|---|
| `publish(path, vt, typeString, propsJson, flags)` | Topic açar; aynı yol + tip için aynı id. Tip çakışması, dolu tablo ya da havuz aşımında `kInvalid`. Runtime başlamadan önce de `kInvalid` |
| `unpublish(id)` | Topic'i kaldırır (DS'e `'R'`) |
| `set(id, data, len)` | Değeri kopyalar; aynıysa düşer (`KEEP_DUPLICATES` hariç) |
| `setProperties(id, json)` / `setFlags(id, flags)` | Yeniden duyurur |
| `addSchema(name, type, data, len)` / `hasSchema(name)` | `struct:X` şemaları |
| `drainWrites(fn, ctx)` | DS yazmalarını sırayla `fn`'e verir; `data` yalnız `fn` içinde geçerli. Tek bir task'tan çağırın |
| `rejectWrite(id, reason)` | DS'e `'K'` reddi gönderir |
| `stats()` | topic sayısı, bayt kullanımı, düşürülenler |

Değer tipleri (`ValueType`, little-endian): 1 boolean (u8), 2 int (i64),
3 float (f32), 4 double (f64), 5 string (UTF-8), 6 raw / `struct:X`,
7-10 boolean/int/float/double dizisi, 11 string dizisi (her eleman
u16 uzunluk + baytlar). Bayraklar: `TELEMETRY` 1, `TUNABLE` 2,
`MUTABLE` 4, `ROBUST` 8, `KEEP_DUPLICATES` 16, `PERSISTENT` 32. DS yalnız
`TUNABLE | MUTABLE` topic'lere yazabilir; uzunluğu tipine uymayan yazma
(ör. 8 bayt olmayan double) `'K'` 4 ile reddedilir. Yazma kuyruğunda topic başına
en son değer kalır; uygulanan her yazmadan sonra robotun değeri DS'e
yeniden gönderilir.

### Config-free başlık ve sürüm makroları

`<probot/api.hpp>` WiFi/batarya makrosu istemez; sketch dışındaki `.cpp`
dosyalarından eklenir. Durum, `probot::ds`, `probot::framework`, telemetri,
gamepad/joystick tipleri ve sürüm makrolarını içerir. `<probot.h>` bunu da
ekler ve runtime'ı (WiFi AP, DS, `setup()`/`loop()`) kurar; yalnız sketch'te
bir kez eklenir.

```cpp
#if defined(PROBOT_VERSION) && PROBOT_VERSION >= PROBOT_VERSION_OF(0, 5, 0)
// ...
#endif
```

`PROBOT_VERSION_MAJOR`, `PROBOT_VERSION_MINOR`, `PROBOT_VERSION_PATCH`,
`PROBOT_VERSION_STRING` de tanımlıdır.

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
| `PROBOT_USER_STACK` | 8192 | User task stack'i, bayt (en az 4096, PB-E107) |
| `PROBOT_FRAMEWORK` | tanımsız | Tanımlıysa OpMode hook'larının hepsi isteğe bağlı (framework katmanı için) |
| `PROBOT_DASH_MAX_TOPICS` | 128 | Dashboard topic sayısı (1-255, PB-E108) |
| `PROBOT_DASH_MAX_SCHEMAS` | 32 | Şema sayısı (0-255) |
| `PROBOT_DASH_MAX_VALUE` | 1024 | Değer / özellik / şema başına bayt (16-16384, PB-E108) |
| `PROBOT_DASH_POOL_BYTES` | 32768 | Ad, tip, özellik, değer ve şemalar için toplam bayt |
| `PROBOT_DASH_WRITE_QUEUE` | 2048 | Bekleyen DS yazmaları için bayt |
| `PROBOT_DASH_PERIOD_MS` | 50 | Dashboard gönderim periyodu (10-250) |
| `PROBOT_DASH_MAX_BYTES_PER_TICK` | 8192 | Periyot başına en çok dashboard baytı |

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
| PB-E107 | Derleme | `PROBOT_USER_STACK` 4096 bayttan küçük |
| PB-E108 | Derleme | Dashboard sınırı geçersiz: `PROBOT_DASH_MAX_TOPICS` 1-255, `PROBOT_DASH_MAX_SCHEMAS` 0-255, `PROBOT_DASH_MAX_VALUE` 16-16384, `PROBOT_DASH_PERIOD_MS` 10-250 dışında |
| PB-E201 | Bağlama | Zorunlu hook tanımsız — `undefined reference to teleopLoop()` vb. Dördü de (boş olsa bile) tanımlanmalı |
| PB-E202 | Bağlama | `setup()`/`loop()` sketch'te tanımlanmış — kütüphaneye aittir, hook'ları kullanın |
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
| `/health` | GET | serbest | `{"rssi":N,"up":ms,"heap":N,"dm":b,"joyAgeMs":N,"sta":N,"disc":N,"dashTopics":N,"dashBytes":N,"dashDropped":N}`: izleme/hakem için. `joyAgeMs`: son joystick paketinin yaşı (-1 = hiç gelmedi), `sta`: bağlı istemci sayısı, `disc`: son kopuşun IEEE reason kodu, `dash*`: dashboard topic sayısı, kullanılan bayt, düşürülen yazma/değer |
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
'W' 0x57  dashboard yazma: [1..2] u16 topic id (LE), [3] u8 değer tipi,
          [4..] değer baytları (en çok PROBOT_DASH_MAX_VALUE). Yalnız
          kuyruğa girer; robot kodu drainWrites() ile uygular
'Q' 0x51  dashboard yeniden eşitleme (tek bayt): tüm şema, duyuru ve
          değerler yeniden gönderilir. Sayfa her bağlantıda ve bilmediği
          bir id gördüğünde yollar
```

`'J'`/`'P'` en çok 47 bayttır; yalnız `'W'` bundan uzun olabilir: en çok
47 ile 4 + `dashMax` (`'S'` çerçevesinde) arasından büyük olanı kadar. Bu
sınırı aşan çerçeve bağlantıyı kapatır; sınır içinde kalıp değeri `dashMax`
bayttan uzun olan `'W'` `'K'` 4 ile reddedilir. Robot açılışta büyük çerçeve
tamponunu ayıramadıysa 47 bayttan uzun her çerçeve bağlantıyı kapatır.

Robot → istemci (push):

```
'S' 0x53  durum+sağlık JSON'u — değişiklikte bir sonraki tick'te
          (250 ms), değişiklik yoksa en geç ~1.25 sn'de bir (heartbeat
          görevi de görür). Alanlar: /getState'in tümü (`estop`: acil
          durdurma kilidi; `batt`: pil gerilimi, 0.0 = veri yok), /health'in
          `dash*` sayaçları dışındaki alanları ve `dashMax` (bir dashboard
          değerinin en çok bayt sayısı; yalnız burada).
'T' 0x54  telemetri tamponu (text) — içerik değiştiğinde
'C' 0x43  şema: u8 ad uzunluğu, ad, u8 tip uzunluğu, tip, u16 uzunluk,
          baytlar (ör. "struct:Pose2d", "structschema")
'A' 0x41  topic duyurusu, tekrarlı: u16 id, u8 değer tipi, u8 bayraklar,
          u8 ad uzunluğu, ad, u8 tip uzunluğu, tip, u16 özellik uzunluğu,
          özellik JSON'u
'V' 0x56  değerler: u32 robot ms, tekrarlı { u16 id, u16 uzunluk, baytlar }.
          Yalnız değişenler, 50 ms'de bir (PROBOT_DASH_PERIOD_MS),
          ~1400 baytta bölünür
'R' 0x52  topic kaldırıldı, tekrarlı: u16 id
'K' 0x4B  yazma reddi, tekrarlı: u16 id, u8 durum (1 bilinmeyen id,
          2 değiştirilemez, 3 tip uyuşmazlığı, 4 çözümlenemedi,
          5 kuyruk dolu)
```

Dashboard çerçevelerinde tüm çok baytlı alanlar little-endian'dır.

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
