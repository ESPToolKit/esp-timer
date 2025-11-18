#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Public types
enum class ESPTimerStatus : uint8_t {
  Invalid = 0,
  Running,
  Paused,
  Stopped,
  Completed
};

struct ESPTimerConfig {
  // Stack sizes per task type (in words on ESP-IDF, bytes on Arduino build)
  uint16_t stack_size_timeout = 4096;
  uint16_t stack_size_interval = 4096;
  uint16_t stack_size_sec = 4096;
  uint16_t stack_size_ms = 4096;
  uint16_t stack_size_min = 4096;

  // Priorities per task
  UBaseType_t priority_timeout = 1;
  UBaseType_t priority_interval = 1;
  UBaseType_t priority_sec = 1;
  UBaseType_t priority_ms = 2;   // default slightly higher as it wakes up more often
  UBaseType_t priority_min = 1;

  // Core affinity (-1 means no pin/any core)
  int8_t core_timeout = -1;
  int8_t core_interval = -1;
  int8_t core_sec = -1;
  int8_t core_ms = -1;
  int8_t core_min = -1;
};

class ESPTimer {
 public:
  void init(const ESPTimerConfig& cfg = ESPTimerConfig());

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
  std::vector<TimeoutItem> timeouts_;
  std::vector<IntervalItem> intervals_;
  std::vector<SecItem> secs_;
  std::vector<MsItem> mss_;
  std::vector<MinItem> mins_;

  // FreeRTOS bits
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t hTimeout_ = nullptr;
  TaskHandle_t hInterval_ = nullptr;
  TaskHandle_t hSec_ = nullptr;
  TaskHandle_t hMs_ = nullptr;
  TaskHandle_t hMin_ = nullptr;

  ESPTimerConfig cfg_{};
  bool initialized_ = false;
  uint32_t nextId_ = 1;

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
