# Probot AutoChooser: Sıfırdan Kullanım Rehberi

> **İnceleme taslağı:** Proje liderinin gözden geçirmesi ve uygun görürse ana docs yapısına eklemesi için hazırlandı.

Bu rehber, tek bir sabit otonom yerine firmware'e eklenmiş birkaç otonom rutinden Driver Station üzerinden seçim yapmayı anlatır. Rutinler robot kodunda derlenir. DS yalnızca hangisinin çalışacağını seçer; seçim sırasında robota yeni kod yüklenmez.

## 1. Hangi yöntemi seçmeliyim?

Tek bir otonomunuz varsa mevcut `autonomousInit()` ve `autonomousLoop()` hook'larını kullanmaya devam edin. AutoChooser, aynı firmware içinde birden fazla otonom seçeneği gerektiğinde kullanılır.

| Kullanım | Gerekli sketch hook'ları |
| --- | --- |
| Tek, sabit otonom | `autonomousInit()`, `autonomousLoop()` |
| DS'ten seçilen birden fazla otonom | `autonomousConfigure()` ve her rutin için init/periodic callback'leri; ayrıca sketch'in zorunlu lifecycle hook'ları |

Bir AutoChooser kataloğu kayıtlıysa otonom dispatch kataloğun seçili rutininin callback'lerini kullanır. Bu durumda global `autonomousInit()` ve `autonomousLoop()` hâlâ tanımlanır; ancak katalog dolu olduğu için seçilen rutin adına çağrılmazlar. Katalog yoksa eski davranış korunur.

## 2. Hook'ların görevleri

- `autonomousConfigure()` boot sırasında, Driver Station başlatılmadan önce bir kez çağrılır. Rutin kataloğunu burada kaydedin. Bu fonksiyon sadece kayıt ve konfigürasyon yapmalı; robotu hareket ettiren kodu buraya koymayın.
- `robotInit()` DS'te **Init**'e basılınca çalışır. Motor sürücüleri, sensörler ve diğer robot donanımı için normal başlangıç işlerini burada yapın. Rutin seçimini burada yeniden kaydetmeyin veya temizlemeyin.
- Rutin init callback'i (örnekte `leftAutoInit`) seçilen otonom gerçekten başlayınca bir kez çalışır. Otonoma özel sayaç/süreç başlangıcını burada yapın.
- Rutin periodic callback'i (örnekte `leftAutoPeriodic`) otonom sürerken tekrar çağrılır. Her çağrı kısa sürede geri dönmelidir.
- Global `autonomousInit()` ve `autonomousLoop()` hook'larını, AutoChooser kullanılan sketch'te de boş bırakabilirsiniz; bunlar Probot lifecycle için zorunlu sketch hook'larıdır ve katalog seçili rutini dispatch eder.

## 3. Çalışma sırası

1. Robot açılır ve `autonomousConfigure()` sabit kataloğu kaydeder.
2. DS bağlanır; katalog varsa otonom seçicisi panelde görünür. Varsayılan varsa seçili olarak gelir.
3. STOP durumunda veya Init sonrasında, Start'a basmadan önce DS'ten rutin seçilebilir.
4. **Init** donanım `robotInit()` başlangıcını çalıştırır; seçimi silmez.
5. **Start** ve Autonomous açıkken DS seçili rutin kimliğini doğrular. Başarılıysa o seçim o çalışma için sabitlenir.
6. Autonomous fazına geçince o rutinin init callback'i bir kez, periodic callback'i ise döngü boyunca tekrar çalışır.
7. Stop veya otonom süresinin bitmesi çalışmayı sonlandırır. Yeni Start'ta DS'teki geçerli seçim yeniden kullanılır.

Bu, seçili dosyanın DS'ten robota kopyalanması değildir. Tüm rutin callback'leri firmware'in içindedir; DS seçimi bunlardan hangisinin çağrılacağını belirler.

## 4. Tam örnek: iki rutin

Bu örneği `examples/AutonomousChooser/AutonomousChooser.ino` içinde de bulabilirsiniz. Gerçek robotta yorum satırlarının yerine motor/sensör komutlarınızı koyun.

```cpp
#define PROBOT_WIFI_AP_SSID     "Probot-Robot"
#define PROBOT_WIFI_AP_PASSWORD "robot1234"
#define PROBOT_WIFI_AP_CHANNEL  6
#include <probot.h>

namespace {
  uint32_t lastMessageMs = 0;

  void leftAutoInit() {
    lastMessageMs = millis();
    probot::println("LeftAuto started");
    // Sol başlangıç pozisyonuna özel hazırlık burada yapılır.
  }

  void leftAutoPeriodic() {
    // Sol başlangıç otonomunun sürüş/mekanizma adımları burada yürütülür.
    if (millis() - lastMessageMs >= 1000) {
      probot::println("LeftAuto running");
      lastMessageMs = millis();
    }
    delay(20); // Örnek döngü aralığı; callback kısa sürede dönmelidir.
  }

  void rightAutoInit() {
    lastMessageMs = millis();
    probot::println("RightAuto started");
    // Sağ başlangıç pozisyonuna özel hazırlık burada yapılır.
  }

  void rightAutoPeriodic() {
    // Sağ başlangıç otonomunun sürüş/mekanizma adımları burada yürütülür.
    if (millis() - lastMessageMs >= 1000) {
      probot::println("RightAuto running");
      lastMessageMs = millis();
    }
    delay(20);
  }
}

// Boot sırasında bir kez çalışır; adlar DS'te de aynen görünür.
void autonomousConfigure() {
  probot::autonomous::clearRoutines();

  const bool leftAdded = probot::autonomous::addRoutine(
      "LeftAuto", leftAutoInit, leftAutoPeriodic, true);
  const bool rightAdded = probot::autonomous::addRoutine(
      "RightAuto", rightAutoInit, rightAutoPeriodic);

  if (!leftAdded || !rightAdded) {
    probot::println("AutoChooser registration failed");
  }
}

// Probot'un temel lifecycle hook'ları sketch'te tanımlı olmalıdır.
void robotInit() {
  // Robot donanımını hazırlayın; otonom kataloğunu burada yeniden kaydetmeyin.
}

void robotEnd() {}
void teleopInit() {}
void teleopLoop() { delay(20); }

// Katalog doluyken seçilen rutine dispatch edilir; eski/fallback hook'larıdır.
void autonomousInit() {}
void autonomousLoop() { delay(20); }
```

`true` son argümanı bu örnekte `LeftAuto`'yu varsayılan yapar. Kullanıcı DS'ten başka rutin seçerse DS seçimi varsayılanın önüne geçer. Varsayılan istemiyorsanız hiçbir kayda `true` vermeyin; o durumda DS'ten rutin seçilmeden Autonomous Start kabul edilmez.

## 5. Ad ve etiket seçimi

En kısa kayıt biçiminde tek isim hem kimlik hem ekranda görünen addır:

```cpp
probot::autonomous::addRoutine(
    "TaxiAndScore", taxiInit, taxiPeriodic, true);
```

DS'te **TaxiAndScore** görünür. Kodda başka bir ad tanımlayıp DS'te ayrı bir etiket göstermek isterseniz iki isimli overload'ı kullanın:

```cpp
probot::autonomous::addRoutine(
    "taxi-score", "Taxi and Score", taxiInit, taxiPeriodic, true);
```

Kimlik benzersiz olmalı, en fazla 15 karakter olmalı ve ASCII harf, rakam, `-` veya `_` içermelidir. Ayrı görünen etiket boş olamaz ve en fazla 31 **bayt** olabilir. Toplam en fazla 8 rutin eklenebilir. İsimler büyük/küçük harfe duyarlı olduğundan `LeftAuto` ile `leftauto` farklı kimliklerdir.

## 6. Birden fazla rutin eklemek

Her rutin için iki callback yazın: bir defalık init ve tekrar eden periodic. Sonra hepsini `autonomousConfigure()` içinde kaydedin:

```cpp
void autonomousConfigure() {
  probot::autonomous::clearRoutines();
  probot::autonomous::addRoutine("LeftAuto", leftInit, leftPeriodic, true);
  probot::autonomous::addRoutine("RightAuto", rightInit, rightPeriodic);
  probot::autonomous::addRoutine("CenterAuto", centerInit, centerPeriodic);
}
```

Her rutin firmware'e eklendiği için yeni rutin eklediğinizde sketch'i yeniden derleyip robota yükleyin. Sonraki seçimlerde rutin değiştirmek için yeniden firmware yüklemesi gerekmez.

## 7. Tek otonom için eski kullanım

Bir chooser'a ihtiyacınız yoksa kataloğu hiç kaydetmeyin. Mevcut kullanımınız çalışmaya devam eder:

```cpp
void autonomousInit() {
  // Sabit otonom için bir defalık hazırlık.
}

void autonomousLoop() {
  // Sabit otonom adımları; her turda geri dönün.
  delay(20);
}
```

`autonomousConfigure()` yoksa Probot `autonomousInit()` ve `autonomousLoop()` fonksiyonlarını doğrudan çağırır. İkisini aynı anda chooser kayıtlarıyla kullanıp katalog callback'lerinin de çağrılmasını beklemeyin; katalog dolu olduğunda katalog seçili rutinin callback'leri kullanılır.

## 8. Driver Station'da kullanım

1. Örneği ESP32-S3'e derleyip yükleyin.
2. Robotun Driver Station Wi-Fi ağına bağlanın ve arayüzü açın.
3. Match Control içindeki **Autonomous Routine** listesinin rutin adlarını gösterdiğini doğrulayın. Liste katalog boot sırasında kaydedildiyse Init öncesinde de görünür.
4. Bir rutin seçin. DS seçimi robota gönderip onaylar; onaylanmayan seçim Start için kullanılamaz.
5. **Init**'e basın. Seçili rutin değişmeden kalmalıdır.
6. Autonomous açıkken **Start**'a basın. Seçilen rutinin `...Init()` mesajını/işaretini ve sonra periodic davranışını kontrol edin.
7. Denemeyi bitirmek için **Stop**'a basın.

İlk denemeyi motorlar yerden kaldırılmışken veya tekerler boştayken yapın. Örnek callback'ler gerçek sürüş komutu içermez.

## 9. Hata ve çalıştırma kuralları

- Kayıt çağrılarının `bool` sonucunu kontrol edin. Hatalı/geçersiz kimlik, boş etiket, eksik callback, tekrar eden kimlik, birden fazla varsayılan veya kapasite aşımı katalog hata bayrağını etkinleştirir.
- Katalog hata bayrağı oluşursa DS hata gösterir ve Autonomous Start'ı reddeder. Kaydı düzeltip firmware'i yeniden yükleyin.
- Varsayılan tanımlanmaması kayıt hatası değildir; ancak DS'ten seçim yapılana kadar Start reddedilir.
- Seçim yalnızca robot STOP veya INITED durumundayken yapılabilir; Autonomous/Teleop çalışırken chooser kilitlenir. Start'ta seçilen callback çifti o çalışma boyunca sabitlenir.
- Her `...Periodic()` kısa sürede dönmelidir. Sonsuz `while` döngüsü, uzun bloklayan bekleme veya 2 saniyeyi aşan tek callback turu loop deadline korumasını tetikleyebilir. Akışı küçük adımlara bölüp sonraki turda devam edin.
- Motor çıkışlarını `robotInit()` veya `autonomousConfigure()` içinde başlatmak yerine, güvenli varsayılanlarda tutun ve yalnızca doğru lifecycle aşamasında hareket komutu verin.

## 10. Kaynaklar

- Örnek sketch: [`examples/AutonomousChooser/AutonomousChooser.ino`](../../examples/AutonomousChooser/AutonomousChooser.ino)
- API referansı: [`API.md`](../../API.md)
- Genel proje rehberi: [`README.md`](../../README.md)
