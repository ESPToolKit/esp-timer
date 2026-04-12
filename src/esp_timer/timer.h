#pragma once

#include "timer_allocator.h"
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <functional>
#include <memory>
#include <vector>

// Public types
enum class ESPTimerStatus : uint8_t { Invalid = 0, Running, Paused, Stopped, Completed };

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
	UBaseType_t priorityMs = 2; // default slightly higher as it wakes up more often
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

	// Fixed slot capacities per timer type. Scheduling returns 0 when a bucket is full.
	uint16_t maxTimeouts = 16;
	uint16_t maxIntervals = 16;
	uint16_t maxSecCounters = 8;
	uint16_t maxMsCounters = 8;
	uint16_t maxMinCounters = 8;
};

class ESPTimer {
  public:
	ESPTimer();
	~ESPTimer();

	void init(const ESPTimerConfig &cfg = ESPTimerConfig());
	void deinit();
	bool isInitialized() const {
		return lifecycleState_.load(std::memory_order_acquire) == LifecycleState::Initialized;
	}

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
	bool clearInterval(uint32_t id);
	bool clearSecCounter(uint32_t id);
	bool clearMsCounter(uint32_t id);
	bool clearMinCounter(uint32_t id);

	// Status
	ESPTimerStatus getStatus(uint32_t id);

  private:
	enum class Type : uint8_t { Timeout, Interval, Sec, Ms, Min };
	enum class LifecycleState : uint8_t {
		Uninitialized,
		Initializing,
		Initialized,
		Deinitializing
	};

	struct BaseItem {
		bool active = false;
		bool executing = false;
		uint32_t id = 0;
		ESPTimerStatus status = ESPTimerStatus::Invalid;
		Type type = Type::Timeout;
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

	struct TimedDispatch {
		size_t index = 0;
	};

	struct SecDispatch {
		size_t index = 0;
		int arg = 0;
	};

	struct MsDispatch {
		size_t index = 0;
		uint32_t arg = 0;
	};

	struct MinDispatch {
		size_t index = 0;
		int arg = 0;
	};

	// Storage per type
	TimerVector<TimeoutItem> timeouts_;
	TimerVector<IntervalItem> intervals_;
	TimerVector<SecItem> secs_;
	TimerVector<MsItem> mss_;
	TimerVector<MinItem> mins_;

	TimerVector<TimedDispatch> timeoutDispatch_;
	TimerVector<TimedDispatch> intervalDispatch_;
	TimerVector<SecDispatch> secDispatch_;
	TimerVector<MsDispatch> msDispatch_;
	TimerVector<MinDispatch> minDispatch_;

	// FreeRTOS bits
	mutable SemaphoreHandle_t mutex_ = nullptr;
	TaskHandle_t hTimeout_ = nullptr;
	TaskHandle_t hInterval_ = nullptr;
	TaskHandle_t hSec_ = nullptr;
	TaskHandle_t hMs_ = nullptr;
	TaskHandle_t hMin_ = nullptr;

	ESPTimerConfig cfg_{};
	std::atomic<bool> running_{false};
	std::atomic<LifecycleState> lifecycleState_{LifecycleState::Uninitialized};
	uint32_t nextId_ = 1;
	bool usePSRAMBuffers_ = false;

	bool lock() const;
	void unlock() const;
	uint32_t nextIdLocked();

	// Task loops
	static void timeoutTaskTrampoline(void *arg);
	static void intervalTaskTrampoline(void *arg);
	static void secTaskTrampoline(void *arg);
	static void msTaskTrampoline(void *arg);
	static void minTaskTrampoline(void *arg);

	void timeoutTask();
	void intervalTask();
	void secTask();
	void msTask();
	void minTask();

	// Helpers
	bool configureStorageLocked();
	void releaseStorageLocked();
	void waitForWorkerExit(TaskHandle_t &handle);
	void markTaskExited(Type type);
	bool tryCreateWorkerLocked(
	    TaskFunction_t fn,
	    const char *name,
	    uint16_t stack,
	    UBaseType_t prio,
	    int8_t core,
	    TaskHandle_t &handle
	);
	ESPTimerConfig normalizeConfig(const ESPTimerConfig &cfg) const;
	ESPTimerStatus getStatusLocked(uint32_t id) const;

	template <typename Item> void resetItem(Item &item, Type type);
	template <typename Item> Item *findItemById(TimerVector<Item> &vec, uint32_t id);
	template <typename Item>
	const Item *findItemById(const TimerVector<Item> &vec, uint32_t id) const;
	template <typename Item> Item *findFreeSlot(TimerVector<Item> &vec);
	template <typename Item> void clearStoppedLocked(TimerVector<Item> &vec, Type type);

	bool pauseItem(Type type, uint32_t id);
	bool resumeItem(Type type, uint32_t id);
	ESPTimerStatus
	togglePause(Type type, uint32_t id); // internal: returns new status or Invalid if not found
	bool clearItem(Type type, uint32_t id);
	ESPTimerStatus getItemStatus(Type type, uint32_t id);
};
