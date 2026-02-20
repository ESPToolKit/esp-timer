#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "timer_allocator.h"

// Public types
enum class ESPTimerStatus : uint8_t {
  Invalid = 0,
  Running,
  Paused,
  Stopped,
  Completed
};

struct ESPTimerConfig {
  // Stack sizes per task type (bytes)
  uint16_t stackSizeTimeout = 4096 * sizeof(StackType_t);
  uint16_t stackSizeInterval = 4096 * sizeof(StackType_t);
  uint16_t stackSizeSec = 4096 * sizeof(StackType_t);
  uint16_t stackSizeMs = 4096 * sizeof(StackType_t);
  uint16_t stackSizeMin = 4096 * sizeof(StackType_t);

  // Priorities per task
  UBaseType_t priorityTimeout = 1;
  UBaseType_t priorityInterval = 1;
  UBaseType_t prioritySec = 1;
  UBaseType_t priorityMs = 2;   // default slightly higher as it wakes up more often
  UBaseType_t priorityMin = 1;

  // Core affinity (-1 means no pin/any core)
  int8_t coreTimeout = -1;
  int8_t coreInterval = -1;
  int8_t coreSec = -1;
  int8_t coreMs = -1;
  int8_t coreMin = -1;

  // Prefer PSRAM-backed buffers for timer-owned dynamic containers.
  // Falls back to default heap automatically when unavailable.
  bool usePSRAMBuffers = false;
};

class ESPTimer {
 public:
  ~ESPTimer();

  void init(const ESPTimerConfig& cfg = ESPTimerConfig());
  void deinit();
  bool initialized() const { return initialized_; }

  // Scheduling
  uint32_t setTimeout(std::function<void()> cb, uint32_t delayMs);
  uint32_t setInterval(std::function<void()> cb, uint32_t periodMs);
  uint32_t setSecCounter(std::function<void(int secLeft)> cb, uint32_t totalMs);
  uint32_t setMsCounter(std::function<void(uint32_t msLeft)> cb, uint32_t totalMs);
  uint32_t setMinCounter(std::function<void(int minLeft)> cb, uint32_t totalMs);

  // Pause: set status to Paused if currently Running; returns true on state change
  bool pauseTimer(uint32_t id);
  bool pauseInterval(uint32_t id);
  bool pauseSecCounter(uint32_t id);
  bool pauseMsCounter(uint32_t id);
  bool pauseMinCounter(uint32_t id);

  // Resume: set status to Running if currently Paused; returns true on state change
  bool resumeTimer(uint32_t id);
  bool resumeInterval(uint32_t id);
  bool resumeSecCounter(uint32_t id);
  bool resumeMsCounter(uint32_t id);
  bool resumeMinCounter(uint32_t id);

  // Toggle running status between Running <-> Paused; returns true if now Running
  bool toggleRunStatusTimer(uint32_t id);
  bool toggleRunStatusInterval(uint32_t id);
  bool toggleRunStatusSecCounter(uint32_t id);
  bool toggleRunStatusMsCounter(uint32_t id);
  bool toggleRunStatusMinCounter(uint32_t id);

  // Clear (stop and remove) timers; returns true on success
  bool clearTimeout(uint32_t id);
  // Backward-compatible alias for clearTimeout
  bool clearTimer(uint32_t id);
  bool clearInterval(uint32_t id);
  bool clearSecCounter(uint32_t id);
  bool clearMsCounter(uint32_t id);
  bool clearMinCounter(uint32_t id);

  // Status
  ESPTimerStatus getStatus(uint32_t id);

 private:
  enum class Type : uint8_t { Timeout, Interval, Sec, Ms, Min };

  struct BaseItem {
    uint32_t id = 0;
    ESPTimerStatus status = ESPTimerStatus::Running;
    Type type;
    uint32_t createdMs = 0;
  };

  struct TimeoutItem : BaseItem {
    std::function<void()> cb;
    uint32_t dueAtMs = 0;
  };

  struct IntervalItem : BaseItem {
    std::function<void()> cb;
    uint32_t periodMs = 0;
    uint32_t lastFireMs = 0;
  };

  struct SecItem : BaseItem {
    std::function<void(int)> cb;
    uint32_t endAtMs = 0;
    uint32_t lastTickMs = 0;
  };

  struct MsItem : BaseItem {
    std::function<void(uint32_t)> cb;
    uint32_t endAtMs = 0;
    uint32_t lastTickMs = 0;
  };

  struct MinItem : BaseItem {
    std::function<void(int)> cb;
    uint32_t endAtMs = 0;
    uint32_t lastTickMs = 0;
  };

  // Storage per type
  TimerVector<TimeoutItem> timeouts_;
  TimerVector<IntervalItem> intervals_;
  TimerVector<SecItem> secs_;
  TimerVector<MsItem> mss_;
  TimerVector<MinItem> mins_;

  // FreeRTOS bits
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t hTimeout_ = nullptr;
  TaskHandle_t hInterval_ = nullptr;
  TaskHandle_t hSec_ = nullptr;
  TaskHandle_t hMs_ = nullptr;
  TaskHandle_t hMin_ = nullptr;

  ESPTimerConfig cfg_{};
  bool initialized_ = false;
  std::atomic<bool> running_{false};
  uint32_t nextId_ = 1;
  bool usePSRAMBuffers_ = false;

  uint32_t nextId();
  void lock();
  void unlock();

  // Task loops
  static void timeoutTaskTrampoline(void* arg);
  static void intervalTaskTrampoline(void* arg);
  static void secTaskTrampoline(void* arg);
  static void msTaskTrampoline(void* arg);
  static void minTaskTrampoline(void* arg);

  void timeoutTask();
  void intervalTask();
  void secTask();
  void msTask();
  void minTask();

  // Helpers
  bool pauseItem(Type type, uint32_t id);
  bool resumeItem(Type type, uint32_t id);
  ESPTimerStatus togglePause(Type type, uint32_t id); // internal: returns new status or Invalid if not found
  bool clearItem(Type type, uint32_t id);
  ESPTimerStatus getItemStatus(Type type, uint32_t id);
};
