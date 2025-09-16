# Roadmap

- v0.1
  - [x] Core API: setTimeout, setInterval
  - [x] Counter APIs: per-second, per-ms, per-minute
  - [x] Pause, Resume, Toggle Run Status, Clear, Status by ID
  - [x] FreeRTOS task per timer type with configurable stack/priority/core
  - [x] Examples and README

- v0.2
  - [x] Add resume* explicit methods
  - [ ] Optional removal policy for completed timers (callbacks to notify)
  - [ ] Optional per-timer core/priority overrides

- v0.3
  - [ ] Power/CPU optimizations (batching, event-driven wakeups)
  - [ ] Optional bound on max timers per type
  - [ ] Better test coverage under PlatformIO (Unity) and CI

Notes
- `setMsCounter` can be heavy; consider adding a rate limit or soft cap.
- Consider API sugar for `clearInterval/clearTimeout` aliases.
