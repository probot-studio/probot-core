# Otonom Rutin Seçici (AutoChooser)

Bu rehber, tek bir sabit otonom yerine firmware'e eklenmiş birkaç otonom rutinden birini Driver Station (DS) üzerinden seçmeyi anlatır. Rutinlerin hepsi robot kodunda derlenir; DS yalnızca hangisinin çalışacağını seçer, robota yeni kod yüklenmez.

Tek bir otonomunuz varsa katalog kaydetmeyin: `autonomousInit()`, `autonomousStart()`, `autonomousLoop()` ve `autonomousStop()` eskisi gibi çalışır.

## 1. Kavramlar

| Ne | Ne zaman çalışır | Görevi |
| --- | --- | --- |
| `autonomousConfigure()` | Açılışta bir kez, DS başlamadan önce | Rutin kataloğunu kaydetmek. Robotu hareket ettirmeyin. |
| `autonomousInit()` / `autonomousInitLoop()` | DS'te **INIT** | Tüm rutinler için ortak hazırlık. Robot INIT'te hareket etmez. |
| Rutinin `start` fonksiyonu | **Start** anında bir kez | `autonomousStart()` yerine: sayaçları sıfırla, hareketi başlat. |
| Rutinin `loop` fonksiyonu | Koşu boyunca her turda | `autonomousLoop()` yerine: adım adım ilerle, kısa sürede dön. |
| `autonomousStop()` | Her çıkışta (süre sonu, Stop, timeout, E-stop) | Motorları durdurmak. |

Katalog kayıtlıyken `autonomousStart()` ve `autonomousLoop()` çağrılmaz; seçili rutinin `start` ve `loop` fonksiyonları çalışır. `autonomousLoop()` ve `autonomousStop()` yine de tanımlanmalıdır (kütüphanenin zorunlu hook'larıdır).

## 2. Örnek: iki rutin

Tam sketch: [`examples/AutonomousChooser/AutonomousChooser.ino`](../../examples/AutonomousChooser/AutonomousChooser.ino).

```cpp
#include <probot.h>

uint32_t routineStartMs = 0;

void leftStart()  { routineStartMs = millis(); /* sol başlangıç hareketi */ }
void leftLoop()   { /* millis() - routineStartMs ile adım adım ilerleyin */ }
void rightStart() { routineStartMs = millis(); }
void rightLoop()  { }

void autonomousConfigure() {
  // id, DS'te görünen ad, start, loop, varsayılan mı
  probot::autonomous::addRoutine("left", "Soldan başlangıç", leftStart, leftLoop, true);
  probot::autonomous::addRoutine("right", "Sağdan başlangıç", rightStart, rightLoop);
}

void autonomousLoop() {}
void autonomousStop() { /* motorları durdurun */ }
void teleopLoop() {}
void teleopStop() {}
```

Son argüman `true` olan rutin varsayılandır. Varsayılan vermezseniz DS'ten bir rutin seçilmeden Autonomous Start kabul edilmez.

## 3. Kimlik (id) ve görünen ad kuralları

- **id**: benzersiz, en fazla **15 karakter**, yalnız İngilizce harf, rakam, `-` ve `_`. **Boşluk ve Türkçe karakter (ç, ğ, ı, ö, ş, ü) olmaz.** Büyük/küçük harf ayrıdır (`LeftAuto` ile `leftauto` farklıdır).
- **Görünen ad**: boş olamaz, en fazla 31 bayt (Türkçe harfler 2 bayt sayılır), satır sonu gibi kontrol karakteri içeremez. Türkçe ve boşluk serbesttir.
- En fazla **8 rutin** kaydedilebilir.

Kısa form tek isim alır; bu isim hem id hem görünen ad olur, bu yüzden **id kurallarına uymak zorundadır**:

```cpp
probot::autonomous::addRoutine("LeftAuto", leftStart, leftLoop, true);   // geçerli
probot::autonomous::addRoutine("Sol Otonom", leftStart, leftLoop);       // HATALI: boşluk
probot::autonomous::addRoutine("SolBaşlangıç", leftStart, leftLoop);     // HATALI: Türkçe harf
```

Boşluklu veya Türkçe bir ad göstermek istiyorsanız uzun formu kullanın: `addRoutine("sol", "Sol Otonom", ...)`.

## 4. Driver Station'da kullanım

1. Sketch'i yükleyin, robotun Wi-Fi ağına bağlanıp DS'i açın.
2. Match Control'de **Autonomous** seçin. Otonom kartında **Otonom rutini** listesi görünür.
3. Bir rutin seçin. Seçim robota gönderilir; robot onaylayınca "Seçili: ..." yazar. Onaylanmamış seçimle Start düğmesi kapalı kalır.
4. **INIT**, ardından **Start**. DS, ekranda gördüğünüz rutinin id'sini Start ile birlikte gönderir; robottaki seçim farklıysa (örneğin başka bir tablet değiştirdiyse) Start reddedilir ve sayfa güncellenir.
5. Koşu boyunca liste kilitlidir. Start'ta seçilen rutin koşu bitene kadar değişmez.

Seçim robotun kalıcı belleğine (NVS) yazılır: reset, brownout veya güç kesintisinden sonra robot son seçimle açılır. Yüklenen yeni sketch'te o id artık yoksa varsayılan rutine dönülür (varsayılan yoksa seçim boş kalır).

Seçim şu durumlarda yapılabilir: robot dururken, mod geçişinde veya INIT sonrasında Start'a basılmadan önce. Koşu sırasında veya Start kabul edildikten sonra robot seçimi `PB-E409` ile reddeder.

## 5. Kayıt hataları

`addRoutine` geçersiz bir kayıtta `false` döner ve **kataloğun tamamı** hatalı sayılır: hatalı bir katalogla otonom başlatılmaz (`PB-E309`). Seri monitörde ve DS'te ilk hatanın nedeni ve ilgili id görünür:

| Kod | Anlamı |
| --- | --- |
| `invalid_id` | id boş, 15 karakterden uzun ya da izin verilmeyen karakter içeriyor |
| `invalid_label` | görünen ad boş, 31 bayttan uzun ya da kontrol karakteri içeriyor |
| `missing_callback` | `start` veya `loop` verilmemiş |
| `duplicate_id` | aynı id ikinci kez kaydedilmiş |
| `multiple_defaults` | birden fazla rutin varsayılan işaretlenmiş |
| `too_many_routines` | 8 rutin sınırı aşılmış |

Kaydı düzeltip sketch'i yeniden yükleyin. Teleop hatalı katalogdan etkilenmez.

## 6. Güvenlik kuralları

- Her `loop` çağrısı kısa sürede dönmelidir. Uzun `delay()`, sonsuz `while` veya `PROBOT_LOOP_DEADLINE_MS` süresini aşan bir tur, deadline korumasını tetikler (girişler sıfırlanır, LED kırmızı). Akışı küçük adımlara bölün.
- Hareketi rutinin `start` fonksiyonunda başlatın; `autonomousConfigure()` veya `autonomousInit()` içinde motor sürmeyin.
- İlk denemeyi tekerler yerden kesilmişken yapın.

## 7. Kaynaklar

- Örnek sketch: [`examples/AutonomousChooser/AutonomousChooser.ino`](../../examples/AutonomousChooser/AutonomousChooser.ino)
- API referansı: [`API.md`](../../API.md), "Otonom rutin seçici" bölümü
