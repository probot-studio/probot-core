#define PROBOT_WIFI_AP_SSID     "Probot-AutoChooser"
#define PROBOT_WIFI_AP_PASSWORD "robot1234"
#define PROBOT_WIFI_AP_CHANNEL  6
#include <probot.h>

namespace {
  uint32_t lastLeftMessage = 0;
  uint32_t lastRightMessage = 0;

  void leftAutoInit() {
    probot::println("Soldan baslangic secildi");
    lastLeftMessage = millis();
  }

  void leftAutoPeriodic() {
    if (millis() - lastLeftMessage >= 1000) {
      probot::println("Soldan baslangic calisiyor");
      lastLeftMessage = millis();
    }
    // Buraya soldan başlama sürüş/mekanizma komutlarını yazın.
    delay(20);
  }

  void rightAutoInit() {
    probot::println("Sagdan baslangic secildi");
    lastRightMessage = millis();
  }

  void rightAutoPeriodic() {
    if (millis() - lastRightMessage >= 1000) {
      probot::println("Sagdan baslangic calisiyor");
      lastRightMessage = millis();
    }
    // Buraya sağdan başlama sürüş/mekanizma komutlarını yazın.
    delay(20);
  }
}

// This hook runs once at boot, before the Driver Station is available.
// The routine name is used as the displayed name in the DS chooser.
void autonomousConfigure() {
  probot::autonomous::clearRoutines();
  const bool leftAdded = probot::autonomous::addRoutine(
      "LeftAuto", leftAutoInit, leftAutoPeriodic, true);
  const bool rightAdded = probot::autonomous::addRoutine(
      "RightAuto", rightAutoInit, rightAutoPeriodic);
  if (!leftAdded || !rightAdded) probot::println("AutoChooser kayit hatasi");
}

void robotInit() {}
void robotEnd() {}
void teleopInit() {}
void teleopLoop() { delay(20); }

// Katalog tanımlı olduğunda kütüphane seçilen rutini dispatch eder.
// Bu iki zorunlu hook katalog kullanılmayan eski sketch'ler için fallback'tir.
void autonomousInit() {}
void autonomousLoop() { delay(20); }
