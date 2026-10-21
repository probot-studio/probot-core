// TankDrive - Çift motorlu tank sürüşü (BTS7960 / IBT-2 tarzı sürücü).
//
// Her motor için iki PWM girişi: RPWM (ileri) ve LPWM (geri).
// Sol çubuk Y -> sol motor, sağ çubuk Y -> sağ motor.
//
// Diğer sürücüler (L298N, TB6612: PWM + DIR pinli) için analogWrite/
// digitalWrite satırlarını kendi sürücünüze göre uyarlayın.
// FTC OpMode akışı: modu seç, INIT ile init(), START ile loop(); STOP stop() çağırır.

#define PROBOT_WIFI_AP_SSID     "Probot"
#define PROBOT_WIFI_AP_PASSWORD "Probot1234"
#define PROBOT_WIFI_AP_CHANNEL  1

#include <probot.h>

// Pinleri kendi kartınıza göre değiştirin
#define LEFT_RPWM   5
#define LEFT_LPWM   6
#define RIGHT_RPWM  7
#define RIGHT_LPWM  8

// Motor yönü ters ise true yapın
#define LEFT_INVERTED  false
#define RIGHT_INVERTED true

void setMotor(uint8_t rpwmPin, uint8_t lpwmPin, float power, bool inverted) {
  if (inverted) power = -power;
  int duty = (int)(fabsf(power) * 255.0f);
  if (duty > 255) duty = 255;
  if (power > 0.02f)       { analogWrite(rpwmPin, duty); analogWrite(lpwmPin, 0); }
  else if (power < -0.02f) { analogWrite(rpwmPin, 0);    analogWrite(lpwmPin, duty); }
  else                     { analogWrite(rpwmPin, 0);    analogWrite(lpwmPin, 0); }
}

void stopMotors() {
  setMotor(LEFT_RPWM, LEFT_LPWM, 0, false);
  setMotor(RIGHT_RPWM, RIGHT_LPWM, 0, false);
}

void initDrive() {
  pinMode(LEFT_RPWM, OUTPUT);
  pinMode(LEFT_LPWM, OUTPUT);
  pinMode(RIGHT_RPWM, OUTPUT);
  pinMode(RIGHT_LPWM, OUTPUT);
  stopMotors();
}

void teleopInit() { initDrive(); }

void teleopLoop() {
  auto js = probot::io::joystick_api::makeDefault();

  // Bağlantı koptuğunda kütüphane eksenleri sıfırlar -> motorlar durur.
  setMotor(LEFT_RPWM,  LEFT_LPWM,  js.getLeftY(),  LEFT_INVERTED);
  setMotor(RIGHT_RPWM, RIGHT_LPWM, js.getRightY(), RIGHT_INVERTED);

  delay(20);
}

void teleopStop() {
  stopMotors();   // TeleOp'tan her çıkışta güvenli konum
}

// Otonom örneği: 2 saniye ileri git, dur.
// ÖNEMLİ: autonomousLoop kısa sürede dönmeli — 2 saniyeden uzun bloke
// olan loop "deadline miss" sayılır: input sıfırlanır, LED kırmızı yanar,
// robot güvende tutulur (task öldürülmez). Bu yüzden delay(2000) yerine
// zaman damgasıyla durum takibi yapılır.
uint32_t autoStartTime = 0;

void autonomousInit() {
  // The robot must not move during INIT: only preparation happens here.
  initDrive();
}

void autonomousStart() {
  // Runs once at START: take the timestamp and start driving.
  autoStartTime = millis();
  setMotor(LEFT_RPWM,  LEFT_LPWM,  0.4f, LEFT_INVERTED);
  setMotor(RIGHT_RPWM, RIGHT_LPWM, 0.4f, RIGHT_INVERTED);
}

void autonomousLoop() {
  if (millis() - autoStartTime >= 2000) {
    stopMotors();
  }
  delay(20);
}

void autonomousStop() {
  stopMotors();   // süre sonu, Stop, timeout ve E-stop aynı güvenli çıkışı kullanır
}
