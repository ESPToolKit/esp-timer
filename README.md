# ESPTimer

Lightweight JS-like timers for ESP32 with non-blocking FreeRTOS tasks.

Features
- `setTimeout` (one-shot), `setInterval` (periodic)
- Counters: per-second, per-millisecond, per-minute with remaining time
- Each timer type runs on its own FreeRTOS task
- Pause (toggle pause/resume), stop, and status query per timer ID

Quick Start
- Include `#include <ESPTimer.h>` and call `timer.init()` once (optionally with `ESPTimerConfig`).
- Use the API similar to JS timers:

```
// Triggers once
uint32_t id1 = timer.setTimeout([](){
  Serial.println("1.5 sec is timed out!");
}, 1500);

// Retriggers every 1500ms
uint32_t id2 = timer.setInterval([](){
  Serial.println("1.5 sec is triggered!");
}, 1500);

// Called every sec for 10000 ms
uint32_t id3 = timer.setSecCounter([](int secLeft){
  Serial.printf("%d sec left so far\n", secLeft);
}, 10000);

// Called every ms for 10000 ms
uint32_t id4 = timer.setMsCounter([](uint32_t msLeft){
  // msLeft can be high frequency; keep work light
}, 10000);

// Called every min for 10000 ms
uint32_t id5 = timer.setMinCounter([](int minLeft){
  Serial.printf("%d min left so far\n", minLeft);
}, 10000);

// Pause/resume (toggle) and stop
timer.pauseInterval(id2); // toggles between Paused <-> Running
timer.stopInterval(id2);

// Status
ESPTimerStatus status = timer.getStatus(id1);
```

Notes
- `pause*` is a toggle: calling on a running timer pauses it; calling again resumes it.
- `setMsCounter` can be CPU intensive; use sparingly and keep callbacks very light.
- Each type uses its own FreeRTOS task. Configure stack, priority, and core with `ESPTimerConfig`.

See `examples/Basic/Basic.ino` for a complete sketch.
