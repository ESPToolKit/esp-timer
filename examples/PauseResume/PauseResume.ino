// Demonstrates pause/resume and status queries
#include <Arduino.h>
#include <ESPTimer.h>

uint32_t intervalId;

void setup() {
  Serial.begin(115200);
  delay(500);

  timer.init();

  intervalId = timer.setInterval([]() {
    Serial.println("Tick from interval");
  }, 1000);

  // Pause after 3s
  timer.setTimeout([]() {
    bool ok = timer.pauseInterval(intervalId);
    Serial.printf("Paused interval: %s\n", ok ? "ok" : "fail");
    auto s = timer.getStatus(intervalId);
    Serial.printf("Status now: %d (Paused=2)\n", (int)s);
  }, 3000);

  // Resume after 6s
  timer.setTimeout([]() {
    bool ok = timer.pauseInterval(intervalId); // toggle back
    Serial.printf("Resumed interval: %s\n", ok ? "ok" : "fail");
    auto s = timer.getStatus(intervalId);
    Serial.printf("Status now: %d (Running=1)\n", (int)s);
  }, 6000);

  // Stop after 10s
  timer.setTimeout([]() {
    bool ok = timer.stopInterval(intervalId);
    Serial.printf("Stopped interval: %s\n", ok ? "ok" : "fail");
    auto s = timer.getStatus(intervalId);
    Serial.printf("Status now: %d (Invalid=0 if removed)\n", (int)s);
  }, 10000);
}

void loop() {}

