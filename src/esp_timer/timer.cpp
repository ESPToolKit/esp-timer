#include "timer.h"

#include <algorithm>
#include <type_traits>

ESPTimer::~ESPTimer() {
  deinit();
}

void ESPTimer::deinit() {
  if (!initialized_) return;

  running_.store(false, std::memory_order_release);

  auto waitForTaskExit = [](TaskHandle_t& handle) {
    if (!handle) {
      return;
    }
    TickType_t start = xTaskGetTickCount();
    while (handle && (xTaskGetTickCount() - start) <= pdMS_TO_TICKS(500)) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (handle) {
      vTaskDelete(handle);
      handle = nullptr;
    }
  };

  waitForTaskExit(hTimeout_);
  waitForTaskExit(hInterval_);
  waitForTaskExit(hSec_);
  waitForTaskExit(hMs_);
  waitForTaskExit(hMin_);

  timeouts_.clear();
  intervals_.clear();
  secs_.clear();
  mss_.clear();
  mins_.clear();

  if (mutex_) {
    vSemaphoreDelete(mutex_);
    mutex_ = nullptr;
  }
  initialized_ = false;
}

// Internal lock utilities
void ESPTimer::lock() {
  if (mutex_) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
  }
}

void ESPTimer::unlock() {
  if (mutex_) {
    xSemaphoreGive(mutex_);
  }
}

uint32_t ESPTimer::nextId() {
  // Simple wrap-safe increment (0 is reserved as invalid)
  uint32_t id = nextId_++;
  if (nextId_ == 0) nextId_ = 1;
  return id;
}

void ESPTimer::init(const ESPTimerConfig& cfg) {
  if (initialized_) return;
  cfg_ = cfg;
  usePSRAMBuffers_ = cfg_.usePSRAMBuffers;

  TimerVector<TimeoutItem> timeoutStorage{TimerAllocator<TimeoutItem>(usePSRAMBuffers_)};
  timeouts_.swap(timeoutStorage);
  TimerVector<IntervalItem> intervalStorage{TimerAllocator<IntervalItem>(usePSRAMBuffers_)};
  intervals_.swap(intervalStorage);
  TimerVector<SecItem> secStorage{TimerAllocator<SecItem>(usePSRAMBuffers_)};
  secs_.swap(secStorage);
  TimerVector<MsItem> msStorage{TimerAllocator<MsItem>(usePSRAMBuffers_)};
  mss_.swap(msStorage);
  TimerVector<MinItem> minStorage{TimerAllocator<MinItem>(usePSRAMBuffers_)};
  mins_.swap(minStorage);

  if (!mutex_) {
    mutex_ = xSemaphoreCreateMutex();
  }

  running_.store(true, std::memory_order_release);
  auto createTask = [&](TaskFunction_t fn,
                        const char* name,
                        uint16_t stack,
                        UBaseType_t prio,
                        int8_t core,
                        TaskHandle_t& handle) {
    const BaseType_t coreId = core < 0 ? tskNO_AFFINITY : static_cast<BaseType_t>(core);
    handle = nullptr;
    const BaseType_t created = xTaskCreatePinnedToCore(
      fn, name ? name : "ESPTimerTask", stack, this, prio, &handle, coreId);
    if (created != pdPASS) {
      handle = nullptr;
    }
  };

  createTask(&ESPTimer::timeoutTaskTrampoline, "ESPTmrTimeout", cfg_.stackSizeTimeout, cfg_.priorityTimeout, cfg_.coreTimeout, hTimeout_);
  createTask(&ESPTimer::intervalTaskTrampoline, "ESPTmrInterval", cfg_.stackSizeInterval, cfg_.priorityInterval, cfg_.coreInterval, hInterval_);
  createTask(&ESPTimer::secTaskTrampoline, "ESPTmrSec", cfg_.stackSizeSec, cfg_.prioritySec, cfg_.coreSec, hSec_);
  createTask(&ESPTimer::msTaskTrampoline, "ESPTmrMs", cfg_.stackSizeMs, cfg_.priorityMs, cfg_.coreMs, hMs_);
  createTask(&ESPTimer::minTaskTrampoline, "ESPTmrMin", cfg_.stackSizeMin, cfg_.priorityMin, cfg_.coreMin, hMin_);

  initialized_ = true;
}

// Public API: creators
uint32_t ESPTimer::setTimeout(std::function<void()> cb, uint32_t delayMs) {
  TimeoutItem item;
  item.type = Type::Timeout;
  item.id = nextId();
  item.cb = std::move(cb);
  item.createdMs = millis();
  item.dueAtMs = item.createdMs + delayMs;
  item.status = ESPTimerStatus::Running;

  lock();
  timeouts_.push_back(std::move(item));
  uint32_t id = timeouts_.back().id;
  unlock();
  return id;
}

uint32_t ESPTimer::setInterval(std::function<void()> cb, uint32_t periodMs) {
  IntervalItem item;
  item.type = Type::Interval;
  item.id = nextId();
  item.cb = std::move(cb);
  item.createdMs = millis();
  item.periodMs = periodMs;
  item.lastFireMs = item.createdMs;
  item.status = ESPTimerStatus::Running;

  lock();
  intervals_.push_back(std::move(item));
  uint32_t id = intervals_.back().id;
  unlock();
  return id;
}

uint32_t ESPTimer::setSecCounter(std::function<void(int)> cb, uint32_t totalMs) {
  SecItem item;
  item.type = Type::Sec;
  item.id = nextId();
  item.cb = std::move(cb);
  item.createdMs = millis();
  item.endAtMs = item.createdMs + totalMs;
  item.lastTickMs = item.createdMs;
  item.status = ESPTimerStatus::Running;

  lock();
  secs_.push_back(std::move(item));
  uint32_t id = secs_.back().id;
  unlock();
  return id;
}

uint32_t ESPTimer::setMsCounter(std::function<void(uint32_t)> cb, uint32_t totalMs) {
  MsItem item;
  item.type = Type::Ms;
  item.id = nextId();
  item.cb = std::move(cb);
  item.createdMs = millis();
  item.endAtMs = item.createdMs + totalMs;
  item.lastTickMs = item.createdMs;
  item.status = ESPTimerStatus::Running;

  lock();
  mss_.push_back(std::move(item));
  uint32_t id = mss_.back().id;
  unlock();
  return id;
}

uint32_t ESPTimer::setMinCounter(std::function<void(int)> cb, uint32_t totalMs) {
  MinItem item;
  item.type = Type::Min;
  item.id = nextId();
  item.cb = std::move(cb);
  item.createdMs = millis();
  item.endAtMs = item.createdMs + totalMs;
  item.lastTickMs = item.createdMs;
  item.status = ESPTimerStatus::Running;

  lock();
  mins_.push_back(std::move(item));
  uint32_t id = mins_.back().id;
  unlock();
  return id;
}

// Public API: pause/clear/status
ESPTimerStatus ESPTimer::togglePause(Type type, uint32_t id) {
  ESPTimerStatus newStatus = ESPTimerStatus::Invalid;
  auto toggle = [&](auto& vec) {
    for (auto& it : vec) {
      if (it.id == id) {
        if (it.status == ESPTimerStatus::Running) {
          it.status = ESPTimerStatus::Paused;
          newStatus = ESPTimerStatus::Paused;
        } else if (it.status == ESPTimerStatus::Paused) {
          it.status = ESPTimerStatus::Running;
          // Shift last tick to avoid immediate burst after long pause
          if constexpr (std::is_same<decltype(it), IntervalItem&>::value) {
            it.lastFireMs = millis();
          } else if constexpr (std::is_same<decltype(it), SecItem&>::value ||
                               std::is_same<decltype(it), MsItem&>::value ||
                               std::is_same<decltype(it), MinItem&>::value) {
            it.lastTickMs = millis();
          }
          newStatus = ESPTimerStatus::Running;
        }
        return;
      }
    }
  };

  lock();
  switch (type) {
    case Type::Timeout: toggle(timeouts_); break;
    case Type::Interval: toggle(intervals_); break;
    case Type::Sec: toggle(secs_); break;
    case Type::Ms: toggle(mss_); break;
    case Type::Min: toggle(mins_); break;
  }
  unlock();
  return newStatus;
}

bool ESPTimer::pauseItem(Type type, uint32_t id) {
  bool changed = false;
  auto pause_fn = [&](auto& vec) {
    for (auto& it : vec) {
      if (it.id == id) {
        if (it.status == ESPTimerStatus::Running) {
          it.status = ESPTimerStatus::Paused;
          changed = true;
        }
        return;
      }
    }
  };

  lock();
  switch (type) {
    case Type::Timeout: pause_fn(timeouts_); break;
    case Type::Interval: pause_fn(intervals_); break;
    case Type::Sec: pause_fn(secs_); break;
    case Type::Ms: pause_fn(mss_); break;
    case Type::Min: pause_fn(mins_); break;
  }
  unlock();
  return changed;
}

bool ESPTimer::resumeItem(Type type, uint32_t id) {
  bool changed = false;
  auto resume_fn = [&](auto& vec) {
    for (auto& it : vec) {
      if (it.id == id) {
        if (it.status == ESPTimerStatus::Paused) {
          it.status = ESPTimerStatus::Running;
          // Shift last firing/tick reference to now to avoid burst
          if constexpr (std::is_same<decltype(it), IntervalItem&>::value) {
            it.lastFireMs = millis();
          } else if constexpr (std::is_same<decltype(it), SecItem&>::value ||
                               std::is_same<decltype(it), MsItem&>::value ||
                               std::is_same<decltype(it), MinItem&>::value) {
            it.lastTickMs = millis();
          }
          changed = true;
        }
        return;
      }
    }
  };

  lock();
  switch (type) {
    case Type::Timeout: resume_fn(timeouts_); break;
    case Type::Interval: resume_fn(intervals_); break;
    case Type::Sec: resume_fn(secs_); break;
    case Type::Ms: resume_fn(mss_); break;
    case Type::Min: resume_fn(mins_); break;
  }
  unlock();
  return changed;
}

bool ESPTimer::clearItem(Type type, uint32_t id) {
  bool removed = false;
  auto remove_by_id = [&](auto& vec) {
    auto it = std::remove_if(vec.begin(), vec.end(), [&](auto& n) {
      if (n.id == id) {
        removed = true;
        return true;
      }
      return false;
    });
    if (it != vec.end()) vec.erase(it, vec.end());
  };

  lock();
  switch (type) {
    case Type::Timeout: remove_by_id(timeouts_); break;
    case Type::Interval: remove_by_id(intervals_); break;
    case Type::Sec: remove_by_id(secs_); break;
    case Type::Ms: remove_by_id(mss_); break;
    case Type::Min: remove_by_id(mins_); break;
  }
  unlock();
  return removed;
}

ESPTimerStatus ESPTimer::getItemStatus(Type type, uint32_t id) {
  ESPTimerStatus status = ESPTimerStatus::Invalid;
  auto find_by_id = [&](auto& vec) {
    for (auto& n : vec) {
      if (n.id == id) {
        status = n.status;
        return;
      }
    }
  };

  lock();
  switch (type) {
    case Type::Timeout: find_by_id(timeouts_); break;
    case Type::Interval: find_by_id(intervals_); break;
    case Type::Sec: find_by_id(secs_); break;
    case Type::Ms: find_by_id(mss_); break;
    case Type::Min: find_by_id(mins_); break;
  }
  unlock();
  return status;
}

bool ESPTimer::pauseTimer(uint32_t id) { return pauseItem(Type::Timeout, id); }
bool ESPTimer::pauseInterval(uint32_t id) { return pauseItem(Type::Interval, id); }
bool ESPTimer::pauseSecCounter(uint32_t id) { return pauseItem(Type::Sec, id); }
bool ESPTimer::pauseMsCounter(uint32_t id) { return pauseItem(Type::Ms, id); }
bool ESPTimer::pauseMinCounter(uint32_t id) { return pauseItem(Type::Min, id); }

bool ESPTimer::resumeTimer(uint32_t id) { return resumeItem(Type::Timeout, id); }
bool ESPTimer::resumeInterval(uint32_t id) { return resumeItem(Type::Interval, id); }
bool ESPTimer::resumeSecCounter(uint32_t id) { return resumeItem(Type::Sec, id); }
bool ESPTimer::resumeMsCounter(uint32_t id) { return resumeItem(Type::Ms, id); }
bool ESPTimer::resumeMinCounter(uint32_t id) { return resumeItem(Type::Min, id); }

bool ESPTimer::toggleRunStatusTimer(uint32_t id) {
  return togglePause(Type::Timeout, id) == ESPTimerStatus::Running;
}
bool ESPTimer::toggleRunStatusInterval(uint32_t id) {
  return togglePause(Type::Interval, id) == ESPTimerStatus::Running;
}
bool ESPTimer::toggleRunStatusSecCounter(uint32_t id) {
  return togglePause(Type::Sec, id) == ESPTimerStatus::Running;
}
bool ESPTimer::toggleRunStatusMsCounter(uint32_t id) {
  return togglePause(Type::Ms, id) == ESPTimerStatus::Running;
}
bool ESPTimer::toggleRunStatusMinCounter(uint32_t id) {
  return togglePause(Type::Min, id) == ESPTimerStatus::Running;
}

bool ESPTimer::clearTimeout(uint32_t id) { return clearItem(Type::Timeout, id); }
bool ESPTimer::clearTimer(uint32_t id) { return clearTimeout(id); }
bool ESPTimer::clearInterval(uint32_t id) { return clearItem(Type::Interval, id); }
bool ESPTimer::clearSecCounter(uint32_t id) { return clearItem(Type::Sec, id); }
bool ESPTimer::clearMsCounter(uint32_t id) { return clearItem(Type::Ms, id); }
bool ESPTimer::clearMinCounter(uint32_t id) { return clearItem(Type::Min, id); }

ESPTimerStatus ESPTimer::getStatus(uint32_t id) {
  // Search across all types; first hit wins
  ESPTimerStatus s;
  s = getItemStatus(Type::Timeout, id); if (s != ESPTimerStatus::Invalid) return s;
  s = getItemStatus(Type::Interval, id); if (s != ESPTimerStatus::Invalid) return s;
  s = getItemStatus(Type::Sec, id); if (s != ESPTimerStatus::Invalid) return s;
  s = getItemStatus(Type::Ms, id); if (s != ESPTimerStatus::Invalid) return s;
  s = getItemStatus(Type::Min, id); if (s != ESPTimerStatus::Invalid) return s;
  return ESPTimerStatus::Invalid;
}

// Task trampolines
void ESPTimer::timeoutTaskTrampoline(void* arg) { static_cast<ESPTimer*>(arg)->timeoutTask(); }
void ESPTimer::intervalTaskTrampoline(void* arg) { static_cast<ESPTimer*>(arg)->intervalTask(); }
void ESPTimer::secTaskTrampoline(void* arg) { static_cast<ESPTimer*>(arg)->secTask(); }
void ESPTimer::msTaskTrampoline(void* arg) { static_cast<ESPTimer*>(arg)->msTask(); }
void ESPTimer::minTaskTrampoline(void* arg) { static_cast<ESPTimer*>(arg)->minTask(); }

// Task loops
void ESPTimer::timeoutTask() {
  while (running_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    TimerVector<std::function<void()>> toCall{TimerAllocator<std::function<void()>>(usePSRAMBuffers_)};

    lock();
    toCall.reserve(timeouts_.size());
    // Collect callbacks due and remove completed
    auto it = timeouts_.begin();
    while (it != timeouts_.end()) {
      if (it->status == ESPTimerStatus::Running && now >= it->dueAtMs) {
        toCall.push_back(it->cb);
        it = timeouts_.erase(it);
      } else if (it->status == ESPTimerStatus::Stopped || it->status == ESPTimerStatus::Completed) {
        it = timeouts_.erase(it);
      } else {
        ++it;
      }
    }
    unlock();

    for (auto& fn : toCall) {
      if (fn) fn();
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  hTimeout_ = nullptr;
  vTaskDelete(nullptr);
}

void ESPTimer::intervalTask() {
  while (running_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    TimerVector<std::function<void()>> toCall{TimerAllocator<std::function<void()>>(usePSRAMBuffers_)};

    lock();
    toCall.reserve(intervals_.size());
    for (auto& it : intervals_) {
      if (it.status == ESPTimerStatus::Running) {
        if (now - it.lastFireMs >= it.periodMs) {
          it.lastFireMs = now;
          toCall.push_back(it.cb);
        }
      }
    }
    // Erase stopped ones
    intervals_.erase(std::remove_if(intervals_.begin(), intervals_.end(), [](const IntervalItem& n) {
                       return n.status == ESPTimerStatus::Stopped || n.status == ESPTimerStatus::Completed;
                     }),
                     intervals_.end());
    unlock();

    for (auto& fn : toCall) {
      if (fn) fn();
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  hInterval_ = nullptr;
  vTaskDelete(nullptr);
}

void ESPTimer::secTask() {
  while (running_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    struct Call { std::function<void(int)> fn; int arg; };
    TimerVector<Call> toCall{TimerAllocator<Call>(usePSRAMBuffers_)};

    lock();
    toCall.reserve(secs_.size());
    for (auto& it : secs_) {
      if (it.status == ESPTimerStatus::Running) {
        if (now - it.lastTickMs >= 1000) {
          it.lastTickMs = now;
          int secLeft = 0;
          if (it.endAtMs > now) {
            uint32_t remaining = it.endAtMs - now;
            // Round up so we only report 0 when no time remains
            secLeft = static_cast<int>((static_cast<uint64_t>(remaining) + 999) / 1000);
          }
          toCall.push_back({it.cb, secLeft});
          if (now >= it.endAtMs) {
            it.status = ESPTimerStatus::Completed;
          }
        }
      }
    }
    secs_.erase(std::remove_if(secs_.begin(), secs_.end(), [](const SecItem& n) {
                      return n.status == ESPTimerStatus::Stopped || n.status == ESPTimerStatus::Completed;
                    }),
                secs_.end());
    unlock();

    for (auto& c : toCall) {
      if (c.fn) c.fn(c.arg);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
  hSec_ = nullptr;
  vTaskDelete(nullptr);
}

void ESPTimer::msTask() {
  while (running_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    struct Call { std::function<void(uint32_t)> fn; uint32_t arg; };
    TimerVector<Call> toCall{TimerAllocator<Call>(usePSRAMBuffers_)};

    lock();
    toCall.reserve(mss_.size());
    for (auto& it : mss_) {
      if (it.status == ESPTimerStatus::Running) {
        // Fire at ~1ms cadence; on busy systems it may be coarser
        if (now - it.lastTickMs >= 1) {
          it.lastTickMs = now;
          uint32_t msLeft = 0;
          if (it.endAtMs > now) msLeft = it.endAtMs - now;
          toCall.push_back({it.cb, msLeft});
          if (now >= it.endAtMs) {
            it.status = ESPTimerStatus::Completed;
          }
        }
      }
    }
    mss_.erase(std::remove_if(mss_.begin(), mss_.end(), [](const MsItem& n) {
                     return n.status == ESPTimerStatus::Stopped || n.status == ESPTimerStatus::Completed;
                   }),
               mss_.end());
    unlock();

    for (auto& c : toCall) {
      if (c.fn) c.fn(c.arg);
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  hMs_ = nullptr;
  vTaskDelete(nullptr);
}

void ESPTimer::minTask() {
  while (running_.load(std::memory_order_acquire)) {
    const uint32_t now = millis();
    struct Call { std::function<void(int)> fn; int arg; };
    TimerVector<Call> toCall{TimerAllocator<Call>(usePSRAMBuffers_)};

    lock();
    toCall.reserve(mins_.size());
    for (auto& it : mins_) {
      if (it.status == ESPTimerStatus::Running) {
        if (now - it.lastTickMs >= 60000) {
          it.lastTickMs = now;
          int minLeft = 0;
          if (it.endAtMs > now) {
            uint32_t remaining = it.endAtMs - now;
            // Round up partial minutes so counters don't drop straight to zero
            minLeft = static_cast<int>((static_cast<uint64_t>(remaining) + 60000 - 1) / 60000);
          }
          toCall.push_back({it.cb, minLeft});
          if (now >= it.endAtMs) {
            it.status = ESPTimerStatus::Completed;
          }
        }
      }
    }
    mins_.erase(std::remove_if(mins_.begin(), mins_.end(), [](const MinItem& n) {
                     return n.status == ESPTimerStatus::Stopped || n.status == ESPTimerStatus::Completed;
                   }),
               mins_.end());
    unlock();

    for (auto& c : toCall) {
      if (c.fn) c.fn(c.arg);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
  hMin_ = nullptr;
  vTaskDelete(nullptr);
}
