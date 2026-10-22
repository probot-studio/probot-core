// JoystickTest - Joystick verilerini seri port ve telemetriye yazdırır.
// FTC OpMode akışı: modu seç, INIT ile init(), START ile loop(); STOP stop() çağırır.

#define PROBOT_WIFI_AP_SSID     "Probot"
#define PROBOT_WIFI_AP_PASSWORD "Probot1234"
#define PROBOT_WIFI_AP_CHANNEL  3

#include <probot.h>

void teleopInit() {
  Serial.begin(115200);
  probot::setLoopPeriodMs(100);   // 10 Hz keeps the Serial output readable
}

void teleopLoop() {
  auto js = probot::io::joystick_api::makeDefault();

  Serial.printf("seq=%lu | LX=%.2f LY=%.2f | RX=%.2f RY=%.2f | LT=%.2f RT=%.2f\n",
                static_cast<unsigned long>(js.getSeq()),
                js.getLeftX(), js.getLeftY(),
                js.getRightX(), js.getRightY(),
                js.getLeftTriggerAxis(), js.getRightTriggerAxis());

  Serial.printf("       | A=%d B=%d X=%d Y=%d | LB=%d RB=%d | POV=%d\n",
                js.getA(), js.getB(), js.getX(), js.getY(),
                js.getLB(), js.getRB(),
                js.getPOV());

  probot::clearTelemetry();
  probot::println("=== JOYSTICK TEST ===");
  probot::printf("Sol:  X=%.2f Y=%.2f\n", js.getLeftX(), js.getLeftY());
  probot::printf("Sag:  X=%.2f Y=%.2f\n", js.getRightX(), js.getRightY());
  probot::printf("LT=%.2f RT=%.2f\n", js.getLeftTriggerAxis(), js.getRightTriggerAxis());

  probot::print("Butonlar: ");
  if (js.getA()) probot::print("A ");
  if (js.getB()) probot::print("B ");
  if (js.getX()) probot::print("X ");
  if (js.getY()) probot::print("Y ");
  if (js.getLB()) probot::print("LB ");
  if (js.getRB()) probot::print("RB ");
  probot::println();

  probot::printf("POV=%d  Seq=%lu\n", js.getPOV(), static_cast<unsigned long>(js.getSeq()));
}

void teleopStop() { probot::setLoopPeriodMs(0); }   // back to USER_LOOP_PERIOD_MS

void autonomousInit() { Serial.begin(115200); }
void autonomousLoop() {}
void autonomousStop() {}
