# ESPTimer

[![CI](https://github.com/ESPToolKit/esp-timer/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-timer/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-timer?sort=semver)](https://github.com/ESPToolKit/esp-timer/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

Lightweight JS-like timers for ESP32 with non-blocking FreeRTOS tasks.

Features
- `setTimeout` (one-shot), `setInterval` (periodic)
- Counters: per-second, per-millisecond, per-minute with remaining time
- Each timer type runs on its own FreeRTOS task
- Pause, resume, toggle run status, clear, and status query per timer ID

Quick Start
- Include `#include <ESPTimer.h>`, create your own `ESPTimer` instance (global, static, or as a class member), and call `.init()` once (optionally with `ESPTimerConfig`).
- Use the API similar to JS timers:

```cpp
ESPTimer timer;

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

// Pause, resume, toggle, and clear
timer.pauseInterval(id2);                         // Pauses if running
timer.resumeInterval(id2);                        // Resumes if paused
bool running = timer.toggleRunStatusInterval(id2); // Toggles; true if now running
timer.clearInterval(id2);

// Status
ESPTimerStatus status = timer.getStatus(id1);
```

Notes
- `pause*` only pauses (idempotent). Use `resume*` to resume, or `toggleRunStatus*` to toggle (returns `true` if now running).
- `setMsCounter` can be CPU intensive; use sparingly and keep callbacks very light.
- Each type uses its own FreeRTOS task. Configure stack, priority, and core with `ESPTimerConfig`.
- Need multiple timer managers? Instantiate as many `ESPTimer` objects as required; each keeps its own FreeRTOS tasks and mutex.

See `examples/Basic/Basic.ino` for a complete sketch.

## ESPToolKit

- Check out other libraries under ESPToolKit: https://github.com/orgs/ESPToolKit/repositories
- Join our discord server at: https://discord.gg/WG8sSqAy
- If you like the libraries, you can support me at: https://ko-fi.com/esptoolkit
