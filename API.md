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
| `utilityInit()`, `utilityInitLoop()`, `utilityStart()`, `utilityLoop()`, `utilityStop()` | Utility modunda, TeleOp karşılıklarıyla aynı zamanlarda | Opsiyonel (weak) |

Akış:

```
STOPPED[mode] ──INIT──▶ INIT[mode] ──START──▶ RUN[mode] ──STOP──▶ STOPPED
                           │                         │
                           └────────STOP────────────┘

RUN[AUTO] ──süre dolar──▶ autonomousStop() ──▶ TRANSITION[TELEOP]
TRANSITION ──INIT──▶ INIT[TELEOP]
```

- Mod yalnız STOPPED/TRANSITION'da seçilir. INIT/RUN sırasında `cmd=mode`
  ve `cmd=opmode` 409 döndürür; önce STOP gerekir.
- **Utility**, kurulum ve bakım kodu (sensör kalibrasyonu, mekanizma
  homing'i, test) için üçüncü moddur. DS'te yalnız sketch `utilityLoop()`
  tanımladıysa ya da bir framework utility seçeneği listelediyse görünür.
  Süresi yoktur ve TRANSITION'a geçmez; input'lar RUN'da açıktır.
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
| `getLeftTriggerAxis()`, `getRightTriggerAxis()` | `float` 0..1 | Tetikler. Tarayıcı tetik için analog değer bildiriyorsa analog (çoğu kumanda), dijital tetikte, klavye/dokunmatikte ve HTTP yedeğinde 0 / 1. Deadzone tetiklere de uygulanır |
| `getA()`, `getB()`, `getX()`, `getY()` | `bool` | Xbox isimleri |
| `getCross()`, `getCircle()`, `getSquare()`, `getTriangle()` | `bool` | PlayStation eşdeğerleri |
| `getLB()`, `getRB()` | `bool` | Omuz butonları |
| `getBack()`, `getStart()`, `getOptions()` | `bool` | Orta butonlar. `getOptions` ortadaki Home/Guide/PS butonudur (standart düzende 16; `tuna-default`'ta 10); PlayStation kumandada "Options" yazan tuş `getStart()`, "Share"/"Create" `getBack()`'tir. İşletim sistemi, Steam ya da tarayıcı bu butonu kendisi kullanabilir, o zaman sayfaya hiç ulaşmaz; 17'den az buton bildiren kumandada her zaman `false` |
| `getLeftStickButton()`, `getRightStickButton()` | `bool` | Çubuğa basma (L3/R3) |
| `getPOV()` | `int` | D-Pad: -1 yok, 0 yukarı, 90 sağ, 180 aşağı, 270 sol. D-Pad'i butondan okuyan eşlemelerde (`logitech-f310`, `standard`) çaprazlar da (45, 135, 225, 315) ve zıt yönler birbirini siler; `axis9-dpad` ve `tuna-default` eksen 9'dan 4 yön okur |
| `getDpadUp()/Right()/Down()/Left()` | `bool` | D-Pad yönü; çaprazda iki yön birden `true` (yukarı+sağ: `getDpadUp()` ve `getDpadRight()`) |
| `getRawAxis(i)`, `getRawButton(i)` | `float` / `bool` | Ham erişim (mapping'siz) |
| `getRawButtonValue(i)` | `float` 0..1 | Butonun analog değeri; DS analog değer yollamadıysa basılıysa 1, değilse 0 |
| `getName(buf, cap)` | `bool` | Tarayıcının verdiği kumanda adı (en çok 63 bayt, UTF-8). Kumanda bağlı değilse `false` ve boş metin |
| `isStandardMapping()` | `bool` | Tarayıcı W3C standart düzeni bildiriyor (`"standard"` eşlemesinin varsaydığı sıra) |
| `isConnected()` | `bool` | En az bir eksen/buton verisi geldi mi |
| `getSeq()`, `getMs()` | `uint32_t` | Paket sayacı / son paket zamanı |
| `getAxisCount()`, `getButtonCount()` | `uint32_t` | Kumandanın bildirdiği sayılar |

**Birden çok kumanda:** DS altı porta kadar kumanda yollar. Port 0
Joystick sekmesinde seçili kaynaktır (kumanda, klavye ya da dokunmatik).
Bağlı diğer tarayıcı kumandaları boş olan en küçük port 1-5'i alır ve
çıkarılana kadar o portta kalır. Klavye ya da dokunmatik port 0'dayken
seçili kumanda da bir port 1-5 alır: sürücü klavyede, operatör kumandada
çalışabilir. Joystick sekmesindeki kumanda kartı port listesini gösterir.

```cpp
auto driver = probot::io::joystick_api::makeDefault();       // port 0
auto op     = probot::io::joystick_api::makeForPort(1);      // port 1
```

Bir kumanda çıkarsa portu hemen nötr okunur ve boş kalır; diğer kumandalar
port değiştirmez. Boşalan port 600 ms başka kumandaya verilmez; port
bekleyen kumanda daha büyük bir porta geçmez, bu süre bitince o portu alır
(kısa bir Bluetooth kopması ya da Joystick sekmesinde seçim değişikliği
kumandayı aynı porta döndürür). Port 0'daki kumanda koparsa port 1-5'teki
bir kumanda port 0'a geçmez: port 0 yeni takılan kumandaya ya da Joystick
sekmesindeki seçime kadar boştur. Her port aynı failsafe ve aynı eşlemeyi (mapping)
kullanır.

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

`printf` bir çağrıda panelin tamamını (256 bayt) yazabilir.

### Konsol

Panel her turda silinip yeniden yazılan bir ekrandır. Kalıcı mesajlar
(uyarı, hata, olay) konsola yazılır: DS'te **Logs** sayfasındaki **Robot
Console** kartında seviye ve robot çalışma süresiyle görünür. Satırlar
silinmez; tampon dolunca en eskileri düşer. Uyarı ve hata gelince menüdeki
**Logs** sekmesinde okunmamış sayısı belirir.

```cpp
probot::console::info("Otonom: %s", "Sol");
probot::console::warning("Batarya düşük: %.1f V", v);
probot::console::error("Kol motoru takıldı");
probot::console::write(probot::console::Level::Error, metin, uzunluk);   // biçimsiz
```

| Çağrı | Açıklama |
|---|---|
| `console::info/warning/error(fmt, ...)` | `printf` biçimiyle bir satır; en çok 512 bayt (fazlası kesilir; iki görev aynı anda 159 bayttan uzun satır yazarsa biri 159 baytta kesilir) |
| `console::write(level, text, len)` | Biçimsiz metin, uzunluk sınırı yok: 512 bayttan uzunsa (mümkünse satır sonundan, hiçbir zaman bir UTF-8 karakterin ortasından değil) birkaç satıra bölünür |
| `console::print(level, text)` | `write` ile aynı, NUL ile biten metin için |

Sondaki `\n` atılır. Tampon boyutu `PROBOT_CONSOLE_BYTES` (varsayılan 4096,
2048-65536, PB-E110); yığından (heap) alınır: açılış bitmeden yazılan
ilk satırda 4096 bayt, sonra açılışta bir kez `PROBOT_CONSOLE_BYTES`
boyutuna taşınır. Bellek yetmezse Serial'e PB-E307 yazılır. Konsol config-free'dir
(`<probot/api.hpp>`), her görevden çağrılabilir (kesme/ISR içinden değil);
loop içinde her turda yazmayın, tampon hızla dolar.

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

Kütüphane bu pini yalnız AUTO_RUN/TELEOP_RUN/UTILITY_RUN'da yanıp söndürür; INIT,
STOPPED, TRANSITION ve E-stop'ta **sabit açık** tutar.

## Robot durumu (ileri seviye)

```cpp
auto s = probot::robot::state().read();   // atomik snapshot
// s.status : Status::INIT/START/STOP
// s.phase  : STOPPED/AUTO_INIT/AUTO_RUN/TELEOP_INIT/TELEOP_RUN/TRANSITION/
//            UTILITY_INIT/UTILITY_RUN
// s.selectedMode : OpMode::AUTO/TELEOP/UTILITY
// s.opModeId : seçili OpMode seçeneğinin id'si (0 = yok)
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
| 6 | `UTILITY_INIT` |
| 7 | `UTILITY_RUN` |

6 ve 7 sonradan eklendi; eski istemciler bunları bilmeyebilir.

`status`, son kabul edilen komut evresini koruyan sade alan olarak
`Status::INIT` (0), `Status::START` (1), `Status::STOP` (2) taşır. Gerçek,
hook'u tamamlanmış evre için daima `phase` kullanılmalıdır.

## Tipli durum: `probot::ds` (ileri seviye)

`#include <probot/api.hpp>` ile her `.cpp` dosyasından kullanılabilir.

| Fonksiyon | Döner |
|---|---|
| `ds::phase()` | Çalışan faz; acil durdurma kilitlendiyse `Phase::STOPPED` |
| `ds::selectedMode()` | DS'te seçili mod (`OpMode::AUTO` / `TELEOP` / `UTILITY`) |
| `ds::selectedOpModeId()` | DS'te seçili OpMode seçeneğinin id'si; seçili modda seçenek yoksa `0`. Otonom süresi dolunca, koşu bitmeden TeleOp seçimine döner |
| `ds::activeOpModeId()` | Şu anki (ya da son) koşunun INIT'te aldığı seçenek id'si; ilk INIT'ten önce `0`. Hook'larda bunu kullanın |
| `ds::utilityAvailable()` | Utility seçilebilir: `utilityLoop()` tanımlı ya da utility seçeneği listelenmiş |
| `ds::outputsEnabled()` | Aktüatör sürülebilir: başlamış RUN, stall yok, acil durdurma yok |
| `ds::isEStopped()` | Acil durdurma kilitli (reboot'a kadar) |
| `ds::isAttached()` | Robotu sahiplenmiş bir DS istemcisi var |
| `ds::matchTimeSeconds()` | Otonom süresinden kalan saniye (AUTO_RUN'da); zamanlı dönem yoksa `-1` |
| `ds::packetSeq()` | Tek sayaç: her portun her DS joystick (`'J'`) paketinde (iki kumandayla her gönderimde iki kez) ve kabul edilen mod/OpMode/INIT/START/STOP komutunda artar (acil durdurma ve reboot isteğinde artmaz) |
| `ds::rslState()` | Robot sinyal ışığı (RSL) şu an yanık mı: robot hareket edebilen RUN fazında yanıp söner, diğer durumlarda sürekli yanar. `PROBOT_RSL_PIN` bağlı olmasa da izlenir; çalışma zamanı başlamadan `false` |
| `ds::commsDisableCount()` | Açılıştan beri DS bağlantısı koptuğu için etkin (START verilmiş) robotun kaç kez durdurulduğu (PB-E303). INIT sırasındaki kopma ve `PROBOT_DS_TIMEOUT_FORCE_STOP 0` iken kopma (PB-E304, yalnız joystick sıfırlanır) sayılmaz |
| `ds::allianceStation()` | DS'te seçilen istasyon: `AllianceStation::Unknown` (seçilmedi), `Red1`..`Red3`, `Blue1`..`Blue3` (WPILib numaralarıyla 0-6) |
| `ds::gameData(out, cap)` | Oyun verisini (UTF-8, en çok 64 bayt) `out`'a kopyalar, `cap > 0` ise NUL ile bitirir; tam uzunluğu `snprintf` gibi döner (`>= cap` ise kesildi). Yoksa `0` ve boş metin |
| `ds::matchInfo()` | Tüm maç bilgisi (`MatchInfo`): `station`, `type` (`MatchType::None` / `Practice` / `Qualification` / `Elimination`), `matchNumber` (0-65535), `replayNumber` (0-255), `eventName`, `gameData` (NUL ile biten, en çok 64 bayt) |
| `ds::matchInfoSeq()` | DS maç bilgisinin herhangi bir alanını her değiştirdiğinde değişir; açılıştan beri hiç değişmediyse `0` |
| `ds::rawGamepad(port, out)` | Kapısız son joystick karesi, her fazda (`port` 0-5). Port yoksa ya da kare yoksa `false` |
| `ds::systemTimeValid()` | Sistem saati (`time()`, `gettimeofday()`) gerçek zamanı tutuyor mu. Robotta pil destekli saat yok: DS sayfası bağlanınca tarayıcının saatini gönderir. İlk geçerli saat her zaman kurulur, sonrakiler yalnız robot durmuşken (STOPPED; otonom ile TeleOp arası sayılmaz), böylece maç sırasında saat atlamaz. O zamana dek `false`: güç verilince `time()` 1970'ten sayar; yazılımla yeniden başlatmada (DS'ten reboot, PB-E305, panic/watchdog) eski gerçek zamandan devam edebilir ama yine geçerli sayılmaz. Dosya adı ya da kayıt zamanı yazmadan önce bakın. Saat UTC'dir; yerel saat için sketch `setenv("TZ", "<+03>-3", 1); tzset();` çağırır |

`probot::io::rawGamepad(port)` aynı kapısız kaynağın servisidir (`port`
verilmezse 0). Menü, otonom
seçimi ve buton eşlemesi içindir; motor **sürmeyin**: aktüatör için kapılı
`probot::io::gamepad()` / `joystick_api` kullanın. Kapısız veri de bağlantı
kesilince `PROBOT_INPUT_TIMEOUT_MS` sonra sıfırlanır.

**Maç bilgisi** (istasyon, oyun verisi, etkinlik adı, maç türü, maç ve
tekrar numarası) Dashboard'daki "Maç Bilgisi" kartından girilir ve yalnız
robot devre dışıyken değişir: STOPPED ya da otonom ile TeleOp arasındaki
TRANSITION (bir sonraki INIT'e dek sürer), bekleyen komut ve acil durdurma
yok. INIT ya da RUN fazında hiç değişmez; otonom ile TeleOp arasında
değişebilir. INIT'te ya da döngüde `ds::matchInfoSeq()` değiştiyse
yeniden okuyun. Bilgi yalnız RAM'de tutulur ve yeniden başlatmada silinir;
açık kalan sayfa, robotta bilgi yokken (`msq` 0) son gördüğü değerleri
aynı robota (aynı SSID) yeniden gönderir. Sayfa değerleri saklamaz:
yeniden yüklenen sayfa ya da başka bir robotun ağına geçen sayfa
göndermez. Metin
alanları UTF-8, en çok 64 bayt ve kontrol karakteri içeremez.
`PROBOT_HAS_DS_MATCH_INFO` bu fonksiyonların varlığını derleme anında
bildirir.

### Batarya okumaları

`probot::io::battery` her yapılandırmada tanımlıdır (`<probot/api.hpp>`
içinde de):

| Fonksiyon | Ne yapar |
|---|---|
| `currentAmps()` | Bataryadan çekilen akım (A; INA'nın VIN+ ucu batarya, VIN- ucu yük tarafında). Akım sensörü yoksa (`PROBOT_BATTERY_INA` tanımlı değil), ilk örnekten önce ya da sensör yanıt vermediyse `NaN` (`isnan()` ile bakın) |
| `setLowVoltage(volt)` | `isLowVoltage()` eşiği (WPILib `setBrownoutVoltage` gibi, ama yalnız bir bayrak: çıkışlar kapatılmaz). `0` kapatır, varsayılan budur. Negatif, `NaN` ya da 100 V ve üstü için `false` döner, eşik değişmez |
| `isLowVoltage()` | DS'in gösterdiği pil gerilimi (ölçülen ya da `setBatteryVoltage()` ile verilen) eşiğin altında. Eşik kapalıyken ya da okuma yokken `false`. ADC ya da INA okuması yumuşatılır ve 0,1 V adımlarla değişir, yük altındaki kısa bir çökme görünmeyebilir; `setBatteryVoltage()` ile verilen değer yumuşatılmadan kullanılır |

### OpMode listesi

Birden çok otonom ya da teleop programı olan kod (ör. bir framework),
seçenekleri DS'e listeler; DS seçili moddaki seçenekleri bir açılır listede
gösterir:

```cpp
static const probot::ds::OpModeOption kOptions[] = {
  // id (0 olmaz, benzersiz), mod, ad, grup (opsiyonel), açıklama (opsiyonel)
  {101, probot::ds::OpMode::AUTO,    "Sol",    "Otonom", "Topla ve çık"},
  {102, probot::ds::OpMode::AUTO,    "Sağ",    "Otonom", nullptr},
  {201, probot::ds::OpMode::TELEOP,  "Arcade", nullptr,  nullptr},
  {301, probot::ds::OpMode::UTILITY, "Kol homing", nullptr, nullptr},
};
// INIT'ten önce çalışan bir sketch hook'u yok: listeyi statik ilklendirme
// ile verin (framework varsa boot'ta da olur). Metinler kopyalanır.
static const bool kListed = probot::ds::setOpModeOptions(kOptions, 4);

void autonomousInit() {
  uint64_t id = probot::ds::activeOpModeId(); // bu koşunun seçeneği
}
```

- En çok 64 seçenek; ad ve grup en çok 63, açıklama 255 bayt (liste ve
  JSON'u heap'te tutulur; uzun açıklamalı büyük bir liste onlarca KB yer). Geçersiz
  liste (`id` 0 ya da tekrarlı, boş ya da sınırı aşan metin, 64'ten fazla
  seçenek, bilinmeyen mod) ya da bellek yetmezse `false` döner, eski liste
  kalır; metin kısaltılmaz. Liste her an değiştirilebilir.
- Mod seçilince o modun son seçilen (yoksa ilk) seçeneği seçilir. Otonom
  süresi dolup TeleOp önseçildiğinde de aynı kural geçerlidir.
- Robot STOPPED iken seçili seçenek yeni listede yoksa, o modun son seçilen
  (yoksa ilk) seçeneğine geçer. INIT/RUN sırasında seçim değişmez; liste o
  sırada değiştiyse düzeltme bir sonraki INIT'te yapılır. Utility seçiliyken
  Utility artık sunulmuyorsa seçim TeleOp'a geçer.

### Loop periyodu

`initLoop`/`loop` hook'ları sabit hızda çağrılır: turlar periyot aralıklı
sabit bir takvimde başlar ve hook'un süresi periyoda dahildir; geç uyanan
bir tur sonrakilerin planlı başlangıcını kaydırmaz.

| Fonksiyon | Açıklama |
|---|---|
| `probot::setLoopPeriodMs(ms)` | Periyodu 1-1000 ms yapar, bir sonraki turdan geçerli; `0` `USER_LOOP_PERIOD_MS`'e döner. Aralık dışında `false` |
| `probot::loopPeriodMs()` | Geçerli periyot (ms) |
| `probot::loopOverruns()` | Açılıştan beri periyodunu aşan tur sayısı. `init`/`start`/`stop` çalıştıran turlar sayılmaz (onların süresi `PROBOT_HOOK_DEADLINE_MS`) |
| `probot::loopGridRestarts()` | Açılıştan beri takvimin yeniden başlama sayısı: geç biten her tur, `init`/`start`/`stop` turları dahil. Değişirse sonraki turlar yeni bir zaman ızgarasında başlar |

Periyodunu aşan bir turdan sonra kaçırılan turlar art arda çalıştırılmaz;
takvim kısa bir beklemeden sonra yeniden başlar. Uyku süresi
`PROBOT_LOOP_DEADLINE_MS`'e sayılmaz, uzun bir periyot stall sayılmaz.
`/health` ve `'S'` çerçevesi `loopMs` ve `overruns` alanlarını taşır; DS
bunları Logs sayfasındaki Network Status kartında gösterir. Loop içinde
`delay()` kullanmayın: kütüphane turları zaten periyoda göre bekletir,
`delay()` tur süresine eklenir ve periyodu geçen tur aşım sayılır.

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

## Dosya paylaşımı (ileri seviye)

Robot programının LittleFS'e ya da SD karta yazdığı dosyalar (örneğin
`.wpilog` kayıtları) DS'in **Logs** sayfasındaki **Dosyalar** kartında
listelenir ve indirilir:

```cpp
#include <LittleFS.h>

// setup() kütüphanenindir: dosya sistemini sketch açılışta, framework
// boot hook'unda bağlar (kullanıcı task'ında, ilk döngüden önce bir kez).
static void shareLogs() {
  if (LittleFS.begin(true)) probot::files::share("logs", LittleFS, "/logs");
}
static const bool kBoot = probot::framework::install({ &shareLogs, nullptr, nullptr, nullptr });
```

Böylece kart açılıştan hemen sonra, acil durdurmadan sonraki yeniden
başlatmada da çalışır. `install()` yalnız bir kez kabul edilir: framework
kuran bir SDK kullanıyorsanız paylaşımı onun açılış hook'unda kurun. Bu
mümkün değilse paylaşımı ilk `autonomousInit()`/`teleopInit()`'te bir kez
kurun; o zaman yeniden başlatmadan sonra dosyalar bir Init ve Stop'tan
sonra görünür.

`bool probot::files::share(ad, fs, klasor = "/")`:

- `ad`: 1-15 karakter, `A-Z a-z 0-9 _ -`. En fazla 4 paylaşım.
- `fs`: bağlanmış herhangi bir Arduino dosya sistemi (`LittleFS`, `SD`,
  `SD_MMC`...). Paylaşım sürdükçe bağlı kalmalı.
- `klasor`: `/` ile başlayan yol; `.` ve `..` içeremez. Yalnız doğrudan bu
  klasördeki dosyalar sunulur, alt klasörler sunulmaz. Listede paylaşım
  başına en fazla 256 dosya görünür (klasör sırasıyla ilk 512 girişe
  bakılır; LittleFS ad sırası tutar, tarihli adlarda en yeni kayıtlar
  listeye girmeyebilir: eski kayıtları silin). Adı 127 bayttan uzun ya da
  kontrol karakteri içeren dosyalar listelenmez.
- Geçersiz ad ya da klasörde ve 4 paylaşım doluyken yeni bir adda `false`
  döner. Aynı adla yeniden çağırmak paylaşımı değiştirir (örneğin kayıt
  LittleFS'ten SD'ye taşınınca).

ESP-IDF VFS ile bağlanmış dosya sistemleri (`fs::FS` nesnesi olmayanlar,
örneğin `esp_vfs_fat_sdspi_mount` ile `/sd`) mutlak VFS yoluyla paylaşılır:
`probot::files::share("sd", "/sd/logs")`. Aynı kurallar geçerlidir;
dosyalar POSIX çağrılarıyla (`opendir`, `stat`, `fopen`) okunur.

`probot::files::unshare(ad)` paylaşımı kaldırır (yoksa `false`). Kaldırılan
ya da değiştirilen paylaşımdan süren aktarım sonraki dosya çağrısında
kesilir; `unshare()` ve değiştiren `share()` bunu en çok 1 s bekler.
Takılan bir kart ya da okumayı bırakan bir tarayıcı aktarımı birkaç saniye
daha sürdürebilir: `probot::files::busy()` herhangi bir paylaşımdan bir
klasör ya da dosya hâlâ açıkken `true` döner. Dosya sistemini ayırmadan
önce paylaşımı kaldırın ve `busy()` `false` olana dek bekleyin.
`PROBOT_HAS_FILES_SHARE` bu özelliklerin varlığını derleme anında bildirir.

Liste ve indirme yalnız robot boştayken (STOPPED/TRANSITION, bekleyen
komut yok) çalışır; flash/SD okuması robot programıyla ve kontrol
bağlantısıyla yarışmasın diye. Robot çalışırken 409 (PB-E409) döner ve
kart bağlantıları pasifleşir. Acil durdurmadan sonra da 409 döner: durdurulan
kullanıcı task'ı dosya sistemi ya da SPI kilidini tutuyor olabilir; kayıtlar
yeniden başlatmadan sonra indirilir. Liste ve indirme ayrı, düşük öncelikli
bir task'ta akar, DS bu sırada yanıt vermeye devam eder; aynı anda tek
aktarım olur (ikincisine 503). INIT kabul edilirse süren aktarım kesilir
(tarayıcı indirmeyi başarısız gösterir, yarım dosya bırakmaz). Büyüyen bir
kayıt, açıldığı andaki boyutuyla gönderilir.

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
`running` değerini çeviren bir düğme gösterilir. Widget gösteren
klasörün diğer topic'leri ağaçta ayrıca listelenmez. `Alerts` klasöründeki
`errors`, `warnings`, `infos` (string[], en az biri) etkin uyarıları
seviyesiyle listeler; `PIDController` klasöründe kazançlar (`p`, `i`, `d`,
`izone`, `setpoint`) ve ardından durum (`measurement`, `error`, ...) WPILib
sırasıyla gelir, diğerleri alfabetik; ayrıca `/Tunables/<ad>` altında, aynı adlı
`/Telemetry/<ad>` tablosundaki `measurement` ile `error` de canlı görünür.
`Scheduler` klasöründe `Names` (string[]) ve `Ids` (int64[]) çalışan
komutları listeler; klasörde `TUNABLE | MUTABLE` bir `Cancel` (int64[])
varsa her komutun yanında bir İptal düğmesi o komutun id'sini `Cancel`'a
yazar (komut durana dek sonraki yazmalar önceki id'leri de taşır, çünkü
robot alınmamış bir yazmanın yerine yenisini koyar; `Cancel`'ın kendi
değeri okunmaz). Düğmeye basıldıktan 2 saniye sonra, robot yazmayı
almışsa ve komut hâlâ çalışıyorsa düğme yeniden "İptal" olur. Düzenler WPILib 2027'ninkidir.
`Field2d` klasöründeki her `struct:Pose2d[]` (raw) topic bir nesnedir (poz
başına 24 bayt: `x` m, `y` m, açı radyan, float64; `Robot` önce): sahada
çizilir; 8'den çok pozu olan nesne yol olarak çizilir, 1 km'den uzak
pozlar çizilmez, boyu 24'ün katı olmayan değer geçersiz sayılır. Salt
okunur nesnenin altında tek pozun konumu ya da poz sayısı ile ilk ve son
nokta yazılır. Yazılabilir nesnenin (`TUNABLE | MUTABLE`) altında pozları
`x, y, derece` biçiminde (pozlar `;` ile ayrılır) düzenleyen bir kutu
vardır; 8'e kadar pozu olan nesnenin pozları sahada da sürüklenebilir
(dokunmatik ekranda "Dokunarak sürükle" açıkken). Dizinin tamamı yazılır.
Program pozu sonraki döngüde yeniden verirse poz eski yerine döner; bu hata
değildir. Saha boyutu ve görseli yayımlanmaz: görünüm pozlara göre büyür.
`Mechanism2d` klasörü WPILib 2027 düzeniyle (`dims` double[] olmalı,
`backgroundColor`, `.type` taşımayan ve `position` [x, y] taşıyan kökler,
`.type` = `line` olan iç içe `angle`, `length`, `color`, `weight` kolları)
çizilir. Beklenen alt topic'ler eksik ya da tipi yanlışsa klasör düz ağaç
olarak gösterilir. `.type` = `DriverStationDisplay` olan klasörün `text`
(string) topic'i `\n` (ya da `\r\n`, `\r`) ile ayrılmış satırlardan oluşan
düz metindir: eş aralıklı yazıyla, boşluklar korunarak gösterilir (ilk 48
satır, satır başına 160 karakter; sekme 8'in katı olan sütuna açılır; renk
yok). Boyu `PROBOT_DASH_MAX_VALUE` ile sınırlıdır: varsayılan 1024 bayt 48
satırın her birine ~20 karakter demektir; dolu bir 48 x 160 ekran (~7,7 KB)
için 8192 gerekir (heap maliyeti makro tablosunda).
`/Telemetry/DriverStationDisplay` (yoksa böyle ilk klasör) Dashboard'da da
"Robot Ekranı" kartında görünür; böyle bir topic yokken kart gizlidir.
Robotun reddettiği bir değer (`set()` sınırı aştı) kartta not edilir.
Metin kutularına boşluk yalnız robot devre dışıyken
(STOPPED ya da TRANSITION) yazılır; diğer fazlarda Space yine acil
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
| `set(id, data, len)` | Değeri kopyalar; aynıysa düşer (`KEEP_DUPLICATES` hariç). Değer, boyu 2'nin kuvvetine yuvarlanmış bir tamponda havuzdan yer alarak tutulur. `PROBOT_DASH_MAX_VALUE`'dan uzunsa ya da havuza sığmazsa `false` döner; DS eski değeri "Güncel değil" diye işaretler (kendi satırı olmayan widget topic'leri widget'ın altında listelenir), kaydedilen sonraki `set()` işareti kaldırır. Bilinmeyen id ya da `len > 0` iken `data == nullptr` da `false` döner (işaretsiz) |
| `setProperties(id, json)` / `setFlags(id, flags)` | Yeniden duyurur |
| `addSchema(name, type, data, len)` / `hasSchema(name)` | `struct:X` ve `proto:X` şemaları; en çok `PROBOT_DASH_MAX_SCHEMA_BYTES`, havuzdan (`PROBOT_DASH_POOL_BYTES`) yer alır. Sığmazsa `false` |
| `drainWrites(fn, ctx)` | DS yazmalarını sırayla `fn`'e verir; `data` yalnız `fn` içinde geçerli. Tek bir task'tan çağırın |
| `rejectWrite(id, reason)` | DS'e `'K'` reddi gönderir; topic'e başka yazma verilmeden önce çağırın. `fn` içinde ya da ~2 saniye içinde reddedilen DS yazması kaydedilmez. Geri yüklenen `PERSISTENT` değerde çerçeve gitmez, kayıt silinir |
| `clearPersisted()` | Kayıtlı bütün `PERSISTENT` değerleri, henüz alınmamış geri yüklemeleri ve henüz kaydedilmemiş DS değerlerini siler; topic'ler değerini korur, sonraki DS yazması yeniden kaydedilir. Flash'a yazar: robot dururken ya da bir Init hook'unda çağırın (çıkışlar o sırada nötrdür) |
| `stats()` | topic sayısı, bayt kullanımı, düşürülenler |
| `limits()` | Geçerli sınırlar (`Limits`: `PROBOT_DASH_*` değerleri, üst sınırlara kırpılmış); `runtime_setup()` öncesinde hepsi 0 |

Değer tipleri (`ValueType`, little-endian): 1 boolean (u8), 2 int (i64),
3 float (f32), 4 double (f64), 5 string (UTF-8), 6 raw / `struct:X`,
7-10 boolean/int/float/double dizisi, 11 string dizisi (her eleman
u16 uzunluk + baytlar). Bayraklar: `TELEMETRY` 1, `TUNABLE` 2,
`MUTABLE` 4, `ROBUST` 8, `KEEP_DUPLICATES` 16, `PERSISTENT` 32. DS yalnız
`TUNABLE | MUTABLE` topic'lere yazabilir; uzunluğu tipine uymayan yazma
(ör. 8 bayt olmayan double) `'K'` 4 ile reddedilir. Raw (6) yazmaların
uzunluğu denetlenmez (yalnız `PROBOT_DASH_MAX_VALUE`): DS dışındaki bir
istemci her uzunlukta yazabilir, bu yüzden robot kodu `w.len` değerini
denetler (ör. `struct:Pose2d[]` için 24'ün katı) ve uymayanı `rejectWrite`
ile reddeder. Yazma kuyruğunda topic başına
en son değer kalır; uygulanan her yazmadan sonra robotun değeri DS'e
yeniden gönderilir.

`PERSISTENT` topic'lerin değeri yeniden başlatmada korunur (NVS, `pb_dash`
alanı). `publish()` kayıtlı değeri yol, değer tipi ve tip yazısı tutuyorsa
bir yazma olarak kuyruğa koyar (yazma kuyruğu sınırından yer almaz):
`publish()` sonrasındaki ilk `drainWrites()` onu DS yazması gibi verir,
robot kodu uygular ve `set()` ile yayınlar. Reddedilirse (`rejectWrite`, bu
topic için başka yazma verilmeden önce) kayıt silinir.

Yalnız robot kodunun kabul ettiği DS yazmaları kaydedilir: `drainWrites()`
ile verilen yazma, reddedilmezse (`rejectWrite` yazmayı alan `fn` içinde
ya da topic'e sonraki yazma verilmeden ve en geç ~2 saniye içinde)
kaydedilir. Kaydedilen, DS'in yazdığı değerdir; robotun `set()` ile
verdiği değer hiçbir zaman kaydedilmez ve kaydı değiştirmez. Kayıt robot
bir OpMode çalıştırmıyorken (durmuş ya da acil durdurulmuş) yazılır, çünkü
flash'a yazma iki çekirdeği de kısa süre durdurur; topic'e bekleyen bir
yazma varken (henüz `drainWrites()` ile alınmamış) yazılmaz. Sürüş
sırasında yapılan ayar robot durunca kaydedilir; o arada güç giderse ya da
topic `unpublish()` edilirse kaybolur. Geri yükleme yalnız topic ilk
açılırken olur; `setFlags()` ile sonradan eklenen `PERSISTENT` yalnız
sonraki DS yazmalarını kaydeder. Kayıt okunamazsa korunur ve bir kez
bildirilir; DS'ten yeni değer yazılınca üstüne yazılır. NVS alanı küçüktür
(varsayılan bölümde ~20 KB, WiFi sürücüsü ve DS kanal ayarı gibi diğer
NVS kullanıcılarıyla ortak): kalıcı değerler en çok 2048 bayt olur (ve
`PROBOT_DASH_MAX_VALUE` kadar, varsayılan 1024); sayı ve kısa metin tutun.
`PROBOT_DASH_MAX_VALUE` sonradan kayıtlı bir değerin boyutunun altına
indirilirse o değer geri yüklenmez. Kaydedilmeyi bekleyen değerler topic
başına en çok iki değer kadar bellek kullanır (her biri en çok 2048 bayt;
havuz sınırına sayılmaz). 2048 bayttan büyük bir değer saklanmaz ve hemen
bildirilir; yazılamayan değer konsola PB-E308 yazar ve DS yeni bir değer
yazana dek yeniden denenmez. Kayıt için bellek ayrılamazsa (PB-E308)
saklama robot yeniden başlatılana dek kapalı kalır.
`PROBOT_HAS_DASH_PERSIST` bu davranışın varlığını derleme anında bildirir.

### Config-free başlık ve sürüm makroları

`<probot/api.hpp>` WiFi/batarya makrosu istemez; sketch dışındaki `.cpp`
dosyalarından eklenir. Durum, `probot::ds`, `probot::framework`, telemetri,
gamepad/joystick tipleri, batarya okumaları, dosya paylaşımı ve sürüm
makrolarını içerir. `<probot.h>` bunu da
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
| `USER_LOOP_PERIOD_MS` | 20 | Loop çağrı periyodu (~50 Hz), sabit hızlı; 1-1000 (PB-E109). Çalışırken `probot::setLoopPeriodMs()` |
| `PROBOT_USER_STACK` | 8192 | User task stack'i, bayt (en az 4096, PB-E107) |
| `PROBOT_FRAMEWORK` | tanımsız | Tanımlıysa OpMode hook'larının hepsi isteğe bağlı (framework katmanı için) |
| `PROBOT_DASH_MAX_TOPICS` | 128 | Dashboard topic sayısı (1-255, PB-E108) |
| `PROBOT_DASH_MAX_SCHEMAS` | 32 | Şema sayısı (0-255) |
| `PROBOT_DASH_MAX_VALUE` | 1024 | Değer / özellik başına bayt (16-16384, PB-E108). Havuzun dışında WebSocket alma tamponu (değer + 4 bayt) ve DS çerçeve tamponu (değer + ~520 bayt ile şema sınırı + ~515 bayttan büyüğü) ayrılır: varsayılanlarla ~5,6 KB, 16384'te ~33 KB heap |
| `PROBOT_DASH_MAX_SCHEMA_BYTES` | 4096 | Şema başına bayt (16-32768, PB-E108); protobuf tanımları bir değerden büyük olabilir. DS çerçeve tamponu en az bu kadar olur (32768'de ~33 KB heap) |
| `PROBOT_DASH_POOL_BYTES` | 32768 | Ad, tip, özellik, değer ve şemalar için toplam bayt |
| `PROBOT_DASH_WRITE_QUEUE` | 2048 | Bekleyen DS yazmaları için bayt |
| `PROBOT_DASH_PERIOD_MS` | 50 | Dashboard gönderim periyodu (10-250) |
| `PROBOT_DASH_MAX_BYTES_PER_TICK` | 8192 | Periyot başına en çok dashboard baytı |
| `PROBOT_CONSOLE_BYTES` | 4096 | Robot konsolu tamponu, bayt (2048-65536, PB-E110) |

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
| PB-E108 | Derleme | Dashboard sınırı geçersiz: `PROBOT_DASH_MAX_TOPICS` 1-255, `PROBOT_DASH_MAX_SCHEMAS` 0-255, `PROBOT_DASH_MAX_VALUE` 16-16384, `PROBOT_DASH_MAX_SCHEMA_BYTES` 16-32768, `PROBOT_DASH_PERIOD_MS` 10-250 dışında |
| PB-E109 | Derleme | `USER_LOOP_PERIOD_MS` 1-1000 ms dışında |
| PB-E110 | Derleme | `PROBOT_CONSOLE_BYTES` 2048-65536 dışında |
| PB-E201 | Bağlama | Zorunlu hook tanımsız — `undefined reference to teleopLoop()` vb. Dördü de (boş olsa bile) tanımlanmalı |
| PB-E202 | Bağlama | `setup()`/`loop()` sketch'te tanımlanmış — kütüphaneye aittir, hook'ları kullanın |
| PB-E301 | Çalışma | Deadline miss / stall: bir `initLoop`/`loop` turu `PROBOT_LOOP_DEADLINE_MS`'i (ya da tek seferlik `init`/`start`/`stop` hook'u `PROBOT_HOOK_DEADLINE_MS`'i) aştı; girişler sıfır, halt-safe |
| PB-E302 | Çalışma | Emergency stop kilitli — reboot gerekli |
| PB-E303 | Çalışma | DS bağlantısı koptu → robot durduruldu (`PROBOT_DS_TIMEOUT_FORCE_STOP=1`) |
| PB-E304 | Çalışma | DS bağlantısı koptu → joystick nötr, yeniden bağlanma bekleniyor (`FORCE_STOP=0`) |
| PB-E305 | Çalışma | E-stop'ta `stop()` hook'u `PROBOT_ESTOP_END_MS` içinde dönmedi → çip reboot |
| PB-E306 | Çalışma | Batarya sensörüne (INA219/INA226) I2C'de ulaşılamıyor — arayüz "Veri yok"a düşer, bağlantı/adres kontrol edin |
| PB-E307 | Çalışma | Açılışta `PROBOT_CONSOLE_BYTES` için bellek yok: konsol mesajda yazan boyutta bir tamponla çalışır (açılıştan önce yazılan satırlar için alınmış 4096 bayt ya da 2048); 0 ise konsol satırları düşer. Değeri küçültün |
| PB-E308 | Çalışma | Dashboard `PERSISTENT` değeri flash'ta (NVS) saklanamadı: yazılamadı (alan dolu, flash hatası ya da değer 2048 bayttan büyük; DS yeni bir değer yazana dek yeniden denenmez), silinemedi, kayıt okunamadı (kayıt korunur, bir kez bildirilir; DS'ten yeni değer yazılınca üstüne yazılır) ya da kayıt için bellek ayrılamadı (saklama yeniden başlatılana dek kapalı). Kalıcı değerleri küçültün ya da `clearPersisted()` çağırın; bellek hatasında belleği azaltın (örneğin daha küçük `PROBOT_DASH_MAX_VALUE`) ve robotu yeniden başlatın |
| PB-E409 | HTTP | Komut geçersiz evrede (409) - `mode`/`init`/`start`/`stop` evre kuralları; `/files`, `/files/get` ve `POST /match` robot boşta (STOPPED/TRANSITION, bekleyen komut yok) değilken ya da acil durdurmadan sonra |

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

Acil durdurma değil, normal durdurma (DS'teki STOP gibi) için:

```cpp
probot::requestStop();     // her task'tan güvenli; durdurulacak bir şey yoksa false
```

Çalışan hook bölünmez: döner, en fazla bir loop turu daha çalışabilir,
sonra aktif modun `stop()`u çalışır, robot STOPPED'a geçer ve DS yeniden
INIT edebilir (DS'teki STOP ile aynı). Kabul edilmiş ama henüz başlamamış
bir INIT/START'ı da iptal eder. Durdurma zaten sürüyorsa ya da acil
durdurma kilitliyse `false` döner.

## HTTP / WebSocket arayüzü

Robot `192.168.4.1:80`'de tek sunucu çalıştırır. Kendi DS istemcinizi
yazacaksanız:

| Endpoint | Metod | Sahiplik | Açıklama |
|---|---|---|---|
| `/` | GET | gerekli | Driver Station arayüzü (SPA) |
| `/joystick` | WS | gerekli | Çift yönlü binary kanal (çerçeve formatları aşağıda) |
| `/updateController` | POST | gerekli | JSON fallback: `{"axes":[...],"buttons":[...]}`, WS koptuğunda (yalnız port 0) |
| `/robotControl?cmd=mode&val=auto\|teleop\|utility` | GET | gerekli | STOPPED/TRANSITION'da modu (ve o modun tercih edilen OpMode seçeneğini) seçer; INIT/RUN'da 409. `utility` yalnız `util` true iken |
| `/robotControl?cmd=opmode&id=N` | GET | gerekli | Listelenmiş bir OpMode seçeneğini (ve modunu) seçer; `id` ondalık metin. Listede yoksa 400, INIT/RUN'da 409 |
| `/opmodes` | GET | gerekli | `{"options":[{"id":"101","mode":"auto","name":"Sol","group":"Otonom","description":"..."}]}`. `id` 64 bitlik olabildiği için metindir. Liste hiç verilmediyse `{"options":[]}`. Liste değişince `omv` artar; yanıt sırasında değişirse bağlantı kapanır, yeniden isteyin |
| `/match` | GET | gerekli | `{"seq":N,"station":0-6,"type":0-3,"match":N,"replay":N,"event":"...","gd":"..."}`: maç bilgisi. `seq` = `msq` (`ds::matchInfoSeq()`); `station` ve `type` WPILib numaralarıyla (0 = yok) |
| `/match` | POST | gerekli | Gövde `application/x-www-form-urlencoded`: `station`, `type`, `match`, `replay` ondalık; `event`, `gd` `encodeURIComponent` ile kodlanmış (`+` boşluk sayılmaz). Olmayan alan korunur; bilinmeyen alan ve `=` içermeyen parça yok sayılır. Geçersiz değer, 64 bayttan uzun, geçersiz UTF-8 ya da kontrol karakterli metin 400 (hiçbir alan değişmez); robot boşta (STOPPED/TRANSITION, bekleyen komut yok) değilse ya da acil durdurmadan sonra 409. Başarıda GET'in yanıtını döner |
| `/files` | GET | gerekli | `{"shares":[{"name":"logs","ok":true,"truncated":false,"files":[{"name":"a.wpilog","size":N,"mtime":T}]}]}`. `mtime` Unix saniye, bilinmiyorsa `0`; `ok` false = klasör açılamadı. Paylaşılan klasör yoksa her istemciye ve her evrede 404; robot boşta değilse 409 |
| `/files/get?share=S&name=N` | GET | gerekli | Dosyanın içeriği (`application/octet-stream`, chunked). `name` `encodeURIComponent` ile kodlanır; `/`, `\`, `.` ve `..` reddedilir (400). Yoksa 404, robot boşta değilse 409, başka indirme sürerken 503 |
| `/robotControl?cmd=init&autoLen=N` | GET | gerekli | Seçili modu INIT'e alır; yalnız STOPPED/TRANSITION |
| `/robotControl?cmd=start&autoLen=N` | GET | gerekli | Seçili modu RUN'a alır; yalnız ilgili INIT fazı |
| `/robotControl?cmd=stop` | GET | gerekli | INIT/RUN'dan (ya da kabul edilmiş ama henüz faza yansımamış bir INIT/START'tan) kooperatif stop; diğer durumlarda 409 |
| `/robotControl?cmd=estop` | GET | gerekli | **Acil durdurma**: kullanıcı task'ı öldürülür, aktif `stop()` watchdog'lu (`PROBOT_ESTOP_END_MS`) çalışır, enable pini kesilir, robot reboot'a kadar kilitlenir |
| `/robotControl?cmd=reboot` | GET | gerekli | Çipi yeniden başlatır (`ESP.restart()`) — estop kilidini temizlemenin yolu |
| `/setChannel?ch=N` | GET | gerekli | Kanalı NVS'e kaydet; 1-13 ise CSA ile **canlı** geçiş (zaten o kanaldaysa `live:false`), `0` = kaydı temizle, açılışta firmware varsayılanına dön. Dönüş: `{"ok":b,"ch":N,"live":b}` |
| `/getState` | GET | gerekli | `{"status":N,"phase":N,"selectedMode":"auto|teleop|utility","autoPeriodSeconds":N,"autoRemainingMs":N,"batt":V.V,"estop":b,"opMode":"N","omv":N,"util":b,"msq":N}` (WS yokken fallback). `opMode`: seçili OpMode seçeneği (`"0"` = yok), `omv`: OpMode listesinin sürüm sayacı, `util`: Utility seçilebilir, `msq`: maç bilgisinin değişiklik sayacı (değişince `/match`'i yeniden okuyun). `batt`: pil gerilimi (bölücü/INA otomatik ya da `setBatteryVoltage()` elle), `0.0` = veri yok |
| `/telemetry` | GET | gerekli | Telemetri tamponunun içeriği (text) (WS yokken fallback) |
| `/getBattery` | GET | serbest | Pil gerilimi (kaynak: `PROBOT_BATTERY_ADC_PIN` bölücü, `PROBOT_BATTERY_INA` sensörü ya da elle `probot::setBatteryVoltage()`) |
| `/health` | GET | serbest | `{"rssi":N,"up":ms,"heap":N,"dm":b,"joyAgeMs":N,"sta":N,"disc":N,"dashTopics":N,"dashBytes":N,"dashDropped":N,"loopMs":N,"overruns":N}`: izleme/hakem için. `joyAgeMs`: son joystick paketinin yaşı (-1 = hiç gelmedi), `sta`: bağlı istemci sayısı, `disc`: son kopuşun IEEE reason kodu, `dash*`: dashboard topic sayısı, kullanılan bayt, düşürülen yazma/değer, `loopMs`: loop periyodu, `overruns`: periyodunu aşan tur sayısı |
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
  [3]      uint8   port (0-5; eski sayfalar 0 yollar)
  [4..]    int16   eksenler, big-endian, değer = float × 32767
  [sonra]  uint8[] butonlar, bit-paketli, LSB önce
  [sonra]  isteğe bağlı: uint8 değer sayısı (≤ buton sayısı), sonra o
           kadar uint8 analog buton değeri (0-255, buton sırasıyla)
'N' 0x4E  kumanda bilgisi: port atanınca/değişince ve 2 sn'de bir:
  [1]      uint8   port (0-5)
  [2]      uint8   bayraklar, bit 0 = W3C standart düzen
  [3]      uint8   ad uzunluğu (maks 63)
  [4..]    ad, UTF-8. Robot adı yalnız o porta kare geldikçe raporlar
'P' 0x50  boşta keepalive (tek bayt) — gamepad yokken 2 sn'de bir;
          owner slotunu ve DS aktivitesini canlı tutar
'W' 0x57  dashboard yazma: [1..2] u16 topic id (LE), [3] u8 değer tipi,
          [4..] değer baytları (en çok PROBOT_DASH_MAX_VALUE). Yalnız
          kuyruğa girer; robot kodu drainWrites() ile uygular
'Q' 0x51  yeniden eşitleme (tek bayt): tüm dashboard şema, duyuru ve
          değerleri ile robotta duran tüm konsol satırları yeniden
          gönderilir. Sayfa her bağlantıda, bilmediği bir dashboard id'si
          ya da konsolda boşluk gördüğünde yollar
'U' 0x55  duvar saati: [1..8] i64 tarayıcının UTC zamanı, Unix ms (LE).
          Sayfa her bağlantıda yollar; 2024-2100 dışı yok sayılır
```

`'J'`/`'N'`/`'P'`/`'U'` en çok 68 bayttır; yalnız `'W'` bundan uzun olabilir: en
çok 68 ile 4 + `dashMax` (`'S'` çerçevesinde) arasından büyük olanı kadar. Bu
sınırı aşan çerçeve bağlantıyı kapatır; sınır içinde kalıp değeri `dashMax`
bayttan uzun olan `'W'` `'K'` 4 ile reddedilir. Robot açılışta büyük çerçeve
tamponunu ayıramadıysa 68 bayttan uzun her çerçeve bağlantıyı kapatır.
Kuralsız `'J'`/`'N'` (port 5'ten büyük, sayı sınırı aşılmış, uzunluk
tutmuyor, butonlardan sonra değer bloğu olmayan fazladan bayt) sessizce
atılır.

Robot → istemci (push):

```
'S' 0x53  durum+sağlık JSON'u — değişiklikte bir sonraki tick'te
          (250 ms), değişiklik yoksa en geç ~1.25 sn'de bir (heartbeat
          görevi de görür). Alanlar: /getState'in tümü (`estop`: acil
          durdurma kilidi; `batt`: pil gerilimi, 0.0 = veri yok), /health'in
          `dash*` sayaçları dışındaki alanları ve `dashMax` (bir dashboard
          değerinin en çok bayt sayısı; yalnız burada).
'T' 0x54  telemetri tamponu (text) — içerik değiştiğinde
'L' 0x4C  konsol satırları, her PROBOT_DASH_PERIOD_MS turunda en çok 4 çerçeve: [1..4] u32
          boot kimliği (her açılışta rastgele), [5..8] u32 robotta duran
          en eski satırın sırası, sonra tekrarlı { u32 sıra, u32 robot ms,
          u8 seviye (0 bilgi, 1 uyarı, 2 hata), u16 uzunluk, UTF-8 metin };
          hepsi little-endian. Aynı sırayı ikinci kez alan istemci atlar
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
'E' 0x45  robotun son set()'i reddedildi, tekrarlı: u16 id, u8 sebep
          (1 değer PROBOT_DASH_MAX_VALUE'dan uzun, 2 havuzda ya da
          heap'te yer yok), u32 reddedilen son değerin uzunluğu. Sebep
          değişince hemen, sonra yalnız topic'in DS'te kalan değeri her
          yeniden gönderildiğinde (yeniden eşitleme, yazma yankısı) o
          değerden sonra gelir;
          topic'in sonraki 'V' kaydı işareti kaldırır (robot sonraki
          set()'i değer aynı olsa da gönderir)
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
