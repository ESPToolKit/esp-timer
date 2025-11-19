// Basic usage of ESPTimer
#include <Arduino.h>
#include <ESPTimer.h>

ESPTimer timer;  // Create your own instance

void setup() {
  Serial.begin(115200);
  delay(500);

  ESPTimerConfig cfg; // defaults are fine
  timer.init(cfg);

  // Triggers once
  uint32_t t1 = timer.setTimeout([]() {
    Serial.println("1.5 sec is timed out!");
  }, 1500);

  // Retriggers every 1500ms
  uint32_t t2 = timer.setInterval([]() {
    Serial.println("1.5 sec is triggered!");
  }, 1500);

  // Called every sec for 10000 ms
  uint32_t t3 = timer.setSecCounter([](int secLeft) {
    Serial.printf("%d sec left so far\n", secLeft);
  }, 10000);

  // Called every ms for 10000 ms
  uint32_t t4 = timer.setMsCounter([](uint32_t msLeft) {
    if (msLeft % 1000 == 0) {
      Serial.printf("%lu ms left so far\n", static_cast<unsigned long>(msLeft));
    }
  }, 10000);

  // Called every min for 10000 ms
  uint32_t t5 = timer.setMinCounter([](int minLeft) {
    Serial.printf("%d min left so far\n", minLeft);
  }, 10000);

  // Example: pause then resume interval, then clear
  timer.setTimeout([t2]() {
    bool paused = timer.pauseInterval(t2); // pause only
    Serial.printf("Interval paused: %s\n", paused ? "ok" : "fail");
  }, 5000);

  timer.setTimeout([t2]() {
    bool resumed = timer.resumeInterval(t2);
    Serial.printf("Interval resumed: %s\n", resumed ? "ok" : "fail");
  }, 8000);

  timer.setTimeout([t2]() {
    bool cleared = timer.clearInterval(t2);
    Serial.printf("Interval cleared: %s\n", cleared ? "ok" : "fail");
  }, 12000);

  // Check status example
  timer.setTimeout([t1]() {
    auto s = timer.getStatus(t1);
    Serial.printf("Timeout status: %d\n", static_cast<int>(s));
  }, 2000);
}

void loop() {
  // App code does other things; timers run in background FreeRTOS tasks
}
