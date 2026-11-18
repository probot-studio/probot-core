// AutonomousChooser - Driver Station'dan otonom rutini seçme.
//
// Sketch, açılışta autonomousConfigure() içinde rutin kataloğunu kaydeder.
// Driver Station'da Autonomous seçilince "Otonom rutini" listesi çıkar;
// seçim robota gönderilir, kalıcı belleğe (NVS) yazılır ve reset sonrası
// geri yüklenir. INIT -> Start, seçili rutinin start() ve loop()
// fonksiyonlarını çalıştırır. Koşu sırasında seçim değiştirilemez.
//
// Rutin kimliği (id): en fazla 15 karakter; yalnız İngilizce harf, rakam,
// '-' ve '_' (boşluk ve Türkçe karakter yok). Görünen ad serbesttir.

#define PROBOT_WIFI_AP_SSID     "Probot-AutoChooser"
#define PROBOT_WIFI_AP_PASSWORD "robot1234"
#define PROBOT_WIFI_AP_CHANNEL  6

#include <probot.h>

namespace {
  uint32_t routineStartMs = 0;
  uint32_t lastMessageMs = 0;

  void report(const char* text) {
    if (millis() - lastMessageMs >= 1000) {
      probot::println(text);
      lastMessageMs = millis();
    }
  }

  // start(): Start anında bir kez çalışır (autonomousStart yerine).
  void leftStart() {
    routineStartMs = millis();
    lastMessageMs = 0;
    probot::println("Soldan başlangıç başladı");
  }

  // loop(): koşu boyunca her turda çalışır (autonomousLoop yerine).
  // Kısa sürede dönmeli; delay() ile uzun bekleme yapmayın.
  void leftLoop() {
    report("Soldan başlangıç çalışıyor");
    // Buraya soldan başlama sürüş/mekanizma komutlarını yazın,
    // örneğin millis() - routineStartMs ile adım adım ilerleyin.
  }

  void rightStart() {
    routineStartMs = millis();
    lastMessageMs = 0;
    probot::println("Sağdan başlangıç başladı");
  }

  void rightLoop() {
    report("Sağdan başlangıç çalışıyor");
  }
}

// Açılışta, Driver Station başlamadan önce bir kez çalışır.
void autonomousConfigure() {
  // Uzun form: id, DS'te görünen ad, start, loop, varsayılan mı.
  probot::autonomous::addRoutine("left", "Soldan başlangıç", leftStart, leftLoop, true);
  probot::autonomous::addRoutine("right", "Sağdan başlangıç", rightStart, rightLoop);
  // Kısa form: ad hem id hem görünen ad olur, id kurallarına uymalı.
  // probot::autonomous::addRoutine("LeftAuto", leftStart, leftLoop);
}

// Katalog varken bu iki hook rutin seçiminden bağımsız çağrılır:
// autonomousInit INIT'te hazırlık için, autonomousStop her çıkışta güvenli
// duruş için. autonomousLoop katalog varken çağrılmaz ama tanımlı olmalı.
void autonomousInit() {}
void autonomousLoop() {}
void autonomousStop() {
  // Motorları burada durdurun: süre sonu, Stop, timeout ve E-stop aynı yoldan geçer.
}

void teleopLoop() {}
void teleopStop() {}
