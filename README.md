# ESPTimer

Lightweight JS-like timers for ESP32 with non-blocking FreeRTOS tasks. ESPTimer mirrors `setTimeout`/`setInterval` plus second/millisecond/minute counters so you can schedule work without sprinkling `delay` everywhere.

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-timer/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-timer/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-timer?sort=semver)](https://github.com/ESPToolKit/esp-timer/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- `setTimeout` (one-shot) and `setInterval` (periodic) helpers with numeric IDs.
- Counter helpers: per-second, per-millisecond, and per-minute callbacks with remaining time.
- Each timer type runs on its own FreeRTOS task, with configurable stack, priority, and core affinity (`ESPTimerConfig`).
- Pause, resume, toggle run status, clear, and query status per timer ID.
- Thread-safe API so multiple tasks can schedule and control timers simultaneously.

## Examples
Include the umbrella header, create an `ESPTimer` instance, and call `init` once:

```cpp
#include <ESPTimer.h>

ESPTimer timer;
volatile bool shouldShutdownTimers = false;

void setup() {
    Serial.begin(115200);
    timer.init();

    timer.setTimeout([](){
        Serial.println("Fired once after 1.5 s");
    }, 1500);

    uint32_t intervalId = timer.setInterval([](){
        Serial.println("1.5 s interval");
    }, 1500);

    timer.setSecCounter([](int secLeft){
        Serial.printf("%d seconds remaining\n", secLeft);
    }, 10000);

    timer.setMsCounter([](uint32_t msLeft){
        // Keep work super light inside ms counters
    }, 1000);

    timer.setMinCounter([](int minLeft){
        Serial.printf("%d minutes remaining\n", minLeft);
    }, 60000);

    timer.pauseInterval(intervalId);
    timer.resumeInterval(intervalId);

    timer.setTimeout([](){
        shouldShutdownTimers = true;
    }, 30000);
}

void loop() {
    if (shouldShutdownTimers && timer.isInitialized()) {
        timer.deinit();
        shouldShutdownTimers = false;
    }
}
```

Explore `examples/Basic/Basic.ino` for a complete sketch that demonstrates all timer types.

## Gotchas
- `setMsCounter` wakes every millisecond; keep callbacks trivial or they will starve other work.
- `pause*` calls are idempotent and only transition `Running → Paused`. Use the matching `resume*` or `toggleRunStatus*` helpers to continue.
- Each timer type owns its own FreeRTOS task. Tune `ESPTimerConfig` when you need larger stacks or different priorities.
- IDs are unique per `ESPTimer` instance. Clearing a timer frees the ID; reusing stale IDs after `clear*` will fail.
- `usePSRAMBuffers = true` is best-effort for timer-owned dynamic buffers. If PSRAM is unavailable, allocation falls back to normal heap automatically.

## API Reference
- `void init(const ESPTimerConfig& cfg = {})` – allocate mutexes and spawn each timer worker with the provided stack/priority/core settings.
- `void deinit()` – idempotently stop all timer workers, clear active timers/counters, and free runtime resources.
- `bool isInitialized() const` – `true` when timer workers and synchronization primitives are active.
- Scheduling helpers
  - `uint32_t setTimeout(std::function<void()> cb, uint32_t delayMs)`
  - `uint32_t setInterval(std::function<void()> cb, uint32_t periodMs)`
  - `uint32_t setSecCounter(std::function<void(int)> cb, uint32_t totalMs)`
  - `uint32_t setMsCounter(std::function<void(uint32_t)> cb, uint32_t totalMs)`
  - `uint32_t setMinCounter(std::function<void(int)> cb, uint32_t totalMs)`
- Control helpers: `pause*`, `resume*`, `toggleRunStatus*`, `clear*`, `ESPTimerStatus getStatus(id)`.
  - Timeout-specific clear: `clearTimeout(id)`.

`ESPTimerConfig` knobs (per task type):
- Stack sizes (`stackSizeTimeout`, `stackSizeInterval`, `stackSizeSec`, `stackSizeMs`, `stackSizeMin`).
- Priorities (`priorityTimeout`, …).
- Core affinity (`core*`, `-1` = no pin).
- Buffer policy (`usePSRAMBuffers`) for timer-owned vectors and callback dispatch staging buffers.

`usePSRAMBuffers` only affects allocations owned by ESPTimer. Callback captures (`std::function`) can still allocate outside this policy depending on capture size and STL behavior.

`ESPTimerStatus` reports `Invalid`, `Running`, `Paused`, `Stopped`, or `Completed`.

Stack sizes are expressed in bytes.

## Restrictions
- Designed for ESP32 boards where FreeRTOS is available (Arduino-ESP32 or ESP-IDF). Other MCUs are untested.
- Requires C++17 due to heavy use of `std::function` and lambdas.
- Each timer type consumes its own FreeRTOS task + stack memory—factor that into your RAM budget when enabling multiple counters.

## Tests
Unity-based smoke tests live in `test/test_basic`. Drop the folder into your PlatformIO workspace (or add your own `platformio.ini` at the repo root) and run `pio test -e esp32dev` against an ESP32 dev kit. The test harness is Arduino friendly and exercises every timer type.

## License
MIT — see [LICENSE.md](LICENSE.md).

## ESPToolKit
- Check out other libraries: <https://github.com/orgs/ESPToolKit/repositories>
- Hang out on Discord: <https://discord.gg/WG8sSqAy>
- Support the project: <https://ko-fi.com/esptoolkit>
- Visit the website: <https://www.esptoolkit.hu/>
