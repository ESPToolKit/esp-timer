# Changelog

All notable changes to this project are documented in this file.

The format follows Keep a Changelog and the project adheres to Semantic Versioning.

## [Unreleased]
### Changed
- Removed the library-provided inline `timer` instance. Sketches should now declare their own `ESPTimer` objects (global, static, or as class members) before calling `init()`, enabling multiple independent timer managers.
- Standardized teardown around `deinit()` + `isInitialized()` and removed the `clearTimer(id)` alias in favor of `clearTimeout(id)`.
- Added `ESPTimerConfig::usePSRAMBuffers` and routed timer-owned persistent/transient vectors through `ESPBufferManager` with safe fallback to default heap.
- Migrated timer lane task creation/lifecycle back to native FreeRTOS task handling (`xTaskCreatePinnedToCore`/`vTaskDelete`).

### Fixed
- Ensured per-second and per-minute countdown timers emit their final tick by rounding up remaining time.
- Added lifecycle test coverage for pre-init `deinit()`, repeated `deinit()`, and `init -> deinit -> init` reinitialization.

### Documentation
- Added an MIT license badge and cross-links to other ESPToolKit libraries in the README.
- Removed the outdated note about API sugar helpers from the roadmap.

## [1.0.0] - 2025-09-25
### Added
- Introduced the FreeRTOS-backed `ESPTimer` API with `init`, `setTimeout`, `setInterval`, and per-second/millisecond/minute countdown helpers.
- Added lifecycle controls (`pause*`, `resume*`, `toggleRunStatus*`, `clear*`, `getStatus`) and the `ESPTimerStatus` state machine for inspecting individual timers.
- Enabled task tuning via `ESPTimerConfig`, allowing stack size, priority, and core affinity customization, with an internal mutex for thread-safe updates.
- Provided example sketches (`examples/Basic`, `examples/PauseResume`) demonstrating one-shot, interval, and pause/resume flows.
- Added unit tests covering timeout, interval, and countdown behaviour via CMake/CTest integration.

### Tooling
- Published Arduino and PlatformIO metadata (`library.json`, `library.properties`) plus top-level CMake integration for builds.
- Set up GitHub CI, release automation, and issue/PR templates to standardize contributions.

### Documentation
- Authored the initial README detailing features, quick start guidance, and usage notes.
- Added a project roadmap, license, and code of conduct to outline direction and expectations.

[Unreleased]: https://github.com/ESPToolKit/esp-timer/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/ESPToolKit/esp-timer/releases/tag/v1.0.0
