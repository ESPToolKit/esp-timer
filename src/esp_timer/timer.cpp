#include "timer.h"

#include <type_traits>
#include <utility>

namespace {
template <typename Callback, typename... Args>
void invokeTimerCallback(const Callback &callback, Args... args) noexcept {
	if (!callback) {
		return;
	}

#if defined(__cpp_exceptions)
	try {
		callback(args...);
	} catch (...) {
		// Library-owned code must never let callbacks unwind through worker tasks.
	}
#else
	callback(args...);
#endif
}
} // namespace

ESPTimer::ESPTimer() {
	mutex_ = xSemaphoreCreateMutex();
}

ESPTimer::~ESPTimer() {
	deinit();
	if (mutex_) {
		vSemaphoreDelete(mutex_);
		mutex_ = nullptr;
	}
}

bool ESPTimer::lock() const {
	if (!mutex_) {
		return false;
	}
	return xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void ESPTimer::unlock() const {
	if (mutex_) {
		xSemaphoreGive(mutex_);
	}
}

uint32_t ESPTimer::nextIdLocked() {
	uint32_t id = nextId_++;
	if (nextId_ == 0) {
		nextId_ = 1;
	}
	return id;
}

ESPTimerConfig ESPTimer::normalizeConfig(const ESPTimerConfig &cfg) const {
	ESPTimerConfig normalized = cfg;
	if (normalized.stackSizeTimeout == 0) {
		normalized.stackSizeTimeout = 4096 * sizeof(StackType_t);
	}
	if (normalized.stackSizeInterval == 0) {
		normalized.stackSizeInterval = 4096 * sizeof(StackType_t);
	}
	if (normalized.stackSizeSec == 0) {
		normalized.stackSizeSec = 4096 * sizeof(StackType_t);
	}
	if (normalized.stackSizeMs == 0) {
		normalized.stackSizeMs = 4096 * sizeof(StackType_t);
	}
	if (normalized.stackSizeMin == 0) {
		normalized.stackSizeMin = 4096 * sizeof(StackType_t);
	}
	return normalized;
}

template <typename Item> void ESPTimer::resetItem(Item &item, Type type) {
	Item cleared{};
	cleared.type = type;
	item = std::move(cleared);
}

template <typename Item> Item *ESPTimer::findFreeSlot(TimerVector<Item> &vec) {
	for (auto &item : vec) {
		if (!item.active) {
			return &item;
		}
	}
	return nullptr;
}

template <typename Item> Item *ESPTimer::findItemById(TimerVector<Item> &vec, uint32_t id) {
	for (auto &item : vec) {
		if (item.active && item.id == id) {
			return &item;
		}
	}
	return nullptr;
}

template <typename Item>
const Item *ESPTimer::findItemById(const TimerVector<Item> &vec, uint32_t id) const {
	for (const auto &item : vec) {
		if (item.active && item.id == id) {
			return &item;
		}
	}
	return nullptr;
}

template <typename Item> void ESPTimer::clearStoppedLocked(TimerVector<Item> &vec, Type type) {
	for (auto &item : vec) {
		if (item.active && !item.executing &&
		    (item.status == ESPTimerStatus::Stopped || item.status == ESPTimerStatus::Completed)) {
			resetItem(item, type);
		}
	}
}

bool ESPTimer::configureStorageLocked() {
	TimerVector<TimeoutItem> timeoutStorage{TimerAllocator<TimeoutItem>(usePSRAMBuffers_)};
	TimerVector<IntervalItem> intervalStorage{TimerAllocator<IntervalItem>(usePSRAMBuffers_)};
	TimerVector<SecItem> secStorage{TimerAllocator<SecItem>(usePSRAMBuffers_)};
	TimerVector<MsItem> msStorage{TimerAllocator<MsItem>(usePSRAMBuffers_)};
	TimerVector<MinItem> minStorage{TimerAllocator<MinItem>(usePSRAMBuffers_)};

	TimerVector<TimedDispatch> timeoutDispatch{TimerAllocator<TimedDispatch>(usePSRAMBuffers_)};
	TimerVector<TimedDispatch> intervalDispatch{TimerAllocator<TimedDispatch>(usePSRAMBuffers_)};
	TimerVector<SecDispatch> secDispatch{TimerAllocator<SecDispatch>(usePSRAMBuffers_)};
	TimerVector<MsDispatch> msDispatch{TimerAllocator<MsDispatch>(usePSRAMBuffers_)};
	TimerVector<MinDispatch> minDispatch{TimerAllocator<MinDispatch>(usePSRAMBuffers_)};

	if (!timerTryAssign(timeoutStorage, cfg_.maxTimeouts, TimeoutItem{})) {
		return false;
	}
	if (!timerTryAssign(intervalStorage, cfg_.maxIntervals, IntervalItem{})) {
		return false;
	}
	if (!timerTryAssign(secStorage, cfg_.maxSecCounters, SecItem{})) {
		return false;
	}
	if (!timerTryAssign(msStorage, cfg_.maxMsCounters, MsItem{})) {
		return false;
	}
	if (!timerTryAssign(minStorage, cfg_.maxMinCounters, MinItem{})) {
		return false;
	}

	if (!timerTryReserve(timeoutDispatch, cfg_.maxTimeouts)) {
		return false;
	}
	if (!timerTryReserve(intervalDispatch, cfg_.maxIntervals)) {
		return false;
	}
	if (!timerTryReserve(secDispatch, cfg_.maxSecCounters)) {
		return false;
	}
	if (!timerTryReserve(msDispatch, cfg_.maxMsCounters)) {
		return false;
	}
	if (!timerTryReserve(minDispatch, cfg_.maxMinCounters)) {
		return false;
	}

	for (auto &item : timeoutStorage) {
		resetItem(item, Type::Timeout);
	}
	for (auto &item : intervalStorage) {
		resetItem(item, Type::Interval);
	}
	for (auto &item : secStorage) {
		resetItem(item, Type::Sec);
	}
	for (auto &item : msStorage) {
		resetItem(item, Type::Ms);
	}
	for (auto &item : minStorage) {
		resetItem(item, Type::Min);
	}

	timeouts_.swap(timeoutStorage);
	intervals_.swap(intervalStorage);
	secs_.swap(secStorage);
	mss_.swap(msStorage);
	mins_.swap(minStorage);

	timeoutDispatch_.swap(timeoutDispatch);
	intervalDispatch_.swap(intervalDispatch);
	secDispatch_.swap(secDispatch);
	msDispatch_.swap(msDispatch);
	minDispatch_.swap(minDispatch);
	return true;
}

void ESPTimer::releaseStorageLocked() {
	TimerVector<TimeoutItem>(TimerAllocator<TimeoutItem>(usePSRAMBuffers_)).swap(timeouts_);
	TimerVector<IntervalItem>(TimerAllocator<IntervalItem>(usePSRAMBuffers_)).swap(intervals_);
	TimerVector<SecItem>(TimerAllocator<SecItem>(usePSRAMBuffers_)).swap(secs_);
	TimerVector<MsItem>(TimerAllocator<MsItem>(usePSRAMBuffers_)).swap(mss_);
	TimerVector<MinItem>(TimerAllocator<MinItem>(usePSRAMBuffers_)).swap(mins_);

	TimerVector<TimedDispatch>(TimerAllocator<TimedDispatch>(usePSRAMBuffers_))
	    .swap(timeoutDispatch_);
	TimerVector<TimedDispatch>(TimerAllocator<TimedDispatch>(usePSRAMBuffers_))
	    .swap(intervalDispatch_);
	TimerVector<SecDispatch>(TimerAllocator<SecDispatch>(usePSRAMBuffers_)).swap(secDispatch_);
	TimerVector<MsDispatch>(TimerAllocator<MsDispatch>(usePSRAMBuffers_)).swap(msDispatch_);
	TimerVector<MinDispatch>(TimerAllocator<MinDispatch>(usePSRAMBuffers_)).swap(minDispatch_);
}

bool ESPTimer::tryCreateWorkerLocked(
    TaskFunction_t fn,
    const char *name,
    uint16_t stack,
    UBaseType_t prio,
    int8_t core,
    TaskHandle_t &handle
) {
	handle = nullptr;
	const BaseType_t coreId = core < 0 ? tskNO_AFFINITY : static_cast<BaseType_t>(core);
	return xTaskCreatePinnedToCore(
	           fn,
	           name ? name : "ESPTimerTask",
	           stack,
	           this,
	           prio,
	           &handle,
	           coreId
	       ) == pdPASS &&
	       handle != nullptr;
}

void ESPTimer::waitForWorkerExit(TaskHandle_t &handle) {
	const TickType_t start = xTaskGetTickCount();
	while (handle && (xTaskGetTickCount() - start) <= pdMS_TO_TICKS(500)) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (handle) {
		vTaskDelete(handle);
		handle = nullptr;
	}
}

void ESPTimer::markTaskExited(Type type) {
	switch (type) {
	case Type::Timeout:
		hTimeout_ = nullptr;
		break;
	case Type::Interval:
		hInterval_ = nullptr;
		break;
	case Type::Sec:
		hSec_ = nullptr;
		break;
	case Type::Ms:
		hMs_ = nullptr;
		break;
	case Type::Min:
		hMin_ = nullptr;
		break;
	}
}

void ESPTimer::init(const ESPTimerConfig &cfg) {
	if (!lock()) {
		return;
	}

	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Uninitialized) {
		unlock();
		return;
	}

	lifecycleState_.store(LifecycleState::Initializing, std::memory_order_release);
	cfg_ = normalizeConfig(cfg);
	usePSRAMBuffers_ = cfg_.usePSRAMBuffers;
	nextId_ = 1;

	if (!configureStorageLocked()) {
		lifecycleState_.store(LifecycleState::Uninitialized, std::memory_order_release);
		unlock();
		return;
	}

	running_.store(true, std::memory_order_release);

	const bool createdTimeout = tryCreateWorkerLocked(
	    &ESPTimer::timeoutTaskTrampoline,
	    "ESPTmrTimeout",
	    cfg_.stackSizeTimeout,
	    cfg_.priorityTimeout,
	    cfg_.coreTimeout,
	    hTimeout_
	);
	const bool createdInterval = createdTimeout && tryCreateWorkerLocked(
	                                                   &ESPTimer::intervalTaskTrampoline,
	                                                   "ESPTmrInterval",
	                                                   cfg_.stackSizeInterval,
	                                                   cfg_.priorityInterval,
	                                                   cfg_.coreInterval,
	                                                   hInterval_
	                                               );
	const bool createdSec = createdInterval && tryCreateWorkerLocked(
	                                               &ESPTimer::secTaskTrampoline,
	                                               "ESPTmrSec",
	                                               cfg_.stackSizeSec,
	                                               cfg_.prioritySec,
	                                               cfg_.coreSec,
	                                               hSec_
	                                           );
	const bool createdMs = createdSec && tryCreateWorkerLocked(
	                                         &ESPTimer::msTaskTrampoline,
	                                         "ESPTmrMs",
	                                         cfg_.stackSizeMs,
	                                         cfg_.priorityMs,
	                                         cfg_.coreMs,
	                                         hMs_
	                                     );
	const bool createdMin = createdMs && tryCreateWorkerLocked(
	                                         &ESPTimer::minTaskTrampoline,
	                                         "ESPTmrMin",
	                                         cfg_.stackSizeMin,
	                                         cfg_.priorityMin,
	                                         cfg_.coreMin,
	                                         hMin_
	                                     );

	if (!(createdTimeout && createdInterval && createdSec && createdMs && createdMin)) {
		running_.store(false, std::memory_order_release);
		lifecycleState_.store(LifecycleState::Deinitializing, std::memory_order_release);
		unlock();

		waitForWorkerExit(hTimeout_);
		waitForWorkerExit(hInterval_);
		waitForWorkerExit(hSec_);
		waitForWorkerExit(hMs_);
		waitForWorkerExit(hMin_);

		if (lock()) {
			releaseStorageLocked();
			lifecycleState_.store(LifecycleState::Uninitialized, std::memory_order_release);
			unlock();
		}
		return;
	}

	lifecycleState_.store(LifecycleState::Initialized, std::memory_order_release);
	unlock();
}

void ESPTimer::deinit() {
	if (!lock()) {
		return;
	}

	const LifecycleState state = lifecycleState_.load(std::memory_order_acquire);
	if (state == LifecycleState::Uninitialized || state == LifecycleState::Deinitializing) {
		unlock();
		return;
	}

	lifecycleState_.store(LifecycleState::Deinitializing, std::memory_order_release);
	running_.store(false, std::memory_order_release);
	unlock();

	waitForWorkerExit(hTimeout_);
	waitForWorkerExit(hInterval_);
	waitForWorkerExit(hSec_);
	waitForWorkerExit(hMs_);
	waitForWorkerExit(hMin_);

	if (!lock()) {
		return;
	}

	releaseStorageLocked();
	nextId_ = 1;
	cfg_ = ESPTimerConfig{};
	usePSRAMBuffers_ = false;
	lifecycleState_.store(LifecycleState::Uninitialized, std::memory_order_release);
	unlock();
}

uint32_t ESPTimer::setTimeout(std::function<void()> cb, uint32_t delayMs) {
	if (!cb || !lock()) {
		return 0;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return 0;
	}

	TimeoutItem *slot = findFreeSlot(timeouts_);
	if (!slot) {
		unlock();
		return 0;
	}

	resetItem(*slot, Type::Timeout);
	slot->active = true;
	slot->id = nextIdLocked();
	slot->status = ESPTimerStatus::Running;
	slot->createdMs = millis();
	slot->dueAtMs = slot->createdMs + delayMs;
	slot->cb = std::move(cb);

	const uint32_t id = slot->id;
	unlock();
	return id;
}

uint32_t ESPTimer::setInterval(std::function<void()> cb, uint32_t periodMs) {
	if (!cb || !lock()) {
		return 0;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return 0;
	}

	IntervalItem *slot = findFreeSlot(intervals_);
	if (!slot) {
		unlock();
		return 0;
	}

	resetItem(*slot, Type::Interval);
	slot->active = true;
	slot->id = nextIdLocked();
	slot->status = ESPTimerStatus::Running;
	slot->createdMs = millis();
	slot->periodMs = periodMs;
	slot->lastFireMs = slot->createdMs;
	slot->cb = std::move(cb);

	const uint32_t id = slot->id;
	unlock();
	return id;
}

uint32_t ESPTimer::setSecCounter(std::function<void(int)> cb, uint32_t totalMs) {
	if (!cb || !lock()) {
		return 0;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return 0;
	}

	SecItem *slot = findFreeSlot(secs_);
	if (!slot) {
		unlock();
		return 0;
	}

	resetItem(*slot, Type::Sec);
	slot->active = true;
	slot->id = nextIdLocked();
	slot->status = ESPTimerStatus::Running;
	slot->createdMs = millis();
	slot->endAtMs = slot->createdMs + totalMs;
	slot->lastTickMs = slot->createdMs;
	slot->cb = std::move(cb);

	const uint32_t id = slot->id;
	unlock();
	return id;
}

uint32_t ESPTimer::setMsCounter(std::function<void(uint32_t)> cb, uint32_t totalMs) {
	if (!cb || !lock()) {
		return 0;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return 0;
	}

	MsItem *slot = findFreeSlot(mss_);
	if (!slot) {
		unlock();
		return 0;
	}

	resetItem(*slot, Type::Ms);
	slot->active = true;
	slot->id = nextIdLocked();
	slot->status = ESPTimerStatus::Running;
	slot->createdMs = millis();
	slot->endAtMs = slot->createdMs + totalMs;
	slot->lastTickMs = slot->createdMs;
	slot->cb = std::move(cb);

	const uint32_t id = slot->id;
	unlock();
	return id;
}

uint32_t ESPTimer::setMinCounter(std::function<void(int)> cb, uint32_t totalMs) {
	if (!cb || !lock()) {
		return 0;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return 0;
	}

	MinItem *slot = findFreeSlot(mins_);
	if (!slot) {
		unlock();
		return 0;
	}

	resetItem(*slot, Type::Min);
	slot->active = true;
	slot->id = nextIdLocked();
	slot->status = ESPTimerStatus::Running;
	slot->createdMs = millis();
	slot->endAtMs = slot->createdMs + totalMs;
	slot->lastTickMs = slot->createdMs;
	slot->cb = std::move(cb);

	const uint32_t id = slot->id;
	unlock();
	return id;
}

ESPTimerStatus ESPTimer::togglePause(Type type, uint32_t id) {
	if (!lock()) {
		return ESPTimerStatus::Invalid;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return ESPTimerStatus::Invalid;
	}

	ESPTimerStatus newStatus = ESPTimerStatus::Invalid;
	auto toggle = [&](auto &vec) {
		if (auto *item = findItemById(vec, id)) {
			if (item->status == ESPTimerStatus::Running) {
				item->status = ESPTimerStatus::Paused;
				newStatus = ESPTimerStatus::Paused;
				return;
			}
			if (item->status == ESPTimerStatus::Paused) {
				item->status = ESPTimerStatus::Running;
				if constexpr (std::is_same_v<std::decay_t<decltype(*item)>, IntervalItem>) {
					item->lastFireMs = millis();
				} else if constexpr (!std::is_same_v<std::decay_t<decltype(*item)>, TimeoutItem>) {
					item->lastTickMs = millis();
				}
				newStatus = ESPTimerStatus::Running;
			}
		}
	};

	switch (type) {
	case Type::Timeout:
		toggle(timeouts_);
		break;
	case Type::Interval:
		toggle(intervals_);
		break;
	case Type::Sec:
		toggle(secs_);
		break;
	case Type::Ms:
		toggle(mss_);
		break;
	case Type::Min:
		toggle(mins_);
		break;
	}

	unlock();
	return newStatus;
}

bool ESPTimer::pauseItem(Type type, uint32_t id) {
	if (!lock()) {
		return false;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return false;
	}

	bool changed = false;
	auto pauseFn = [&](auto &vec) {
		if (auto *item = findItemById(vec, id)) {
			if (item->status == ESPTimerStatus::Running) {
				item->status = ESPTimerStatus::Paused;
				changed = true;
			}
		}
	};

	switch (type) {
	case Type::Timeout:
		pauseFn(timeouts_);
		break;
	case Type::Interval:
		pauseFn(intervals_);
		break;
	case Type::Sec:
		pauseFn(secs_);
		break;
	case Type::Ms:
		pauseFn(mss_);
		break;
	case Type::Min:
		pauseFn(mins_);
		break;
	}

	unlock();
	return changed;
}

bool ESPTimer::resumeItem(Type type, uint32_t id) {
	if (!lock()) {
		return false;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return false;
	}

	bool changed = false;
	auto resumeFn = [&](auto &vec) {
		if (auto *item = findItemById(vec, id)) {
			if (item->status == ESPTimerStatus::Paused) {
				item->status = ESPTimerStatus::Running;
				if constexpr (std::is_same_v<std::decay_t<decltype(*item)>, IntervalItem>) {
					item->lastFireMs = millis();
				} else if constexpr (!std::is_same_v<std::decay_t<decltype(*item)>, TimeoutItem>) {
					item->lastTickMs = millis();
				}
				changed = true;
			}
		}
	};

	switch (type) {
	case Type::Timeout:
		resumeFn(timeouts_);
		break;
	case Type::Interval:
		resumeFn(intervals_);
		break;
	case Type::Sec:
		resumeFn(secs_);
		break;
	case Type::Ms:
		resumeFn(mss_);
		break;
	case Type::Min:
		resumeFn(mins_);
		break;
	}

	unlock();
	return changed;
}

bool ESPTimer::clearItem(Type type, uint32_t id) {
	if (!lock()) {
		return false;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return false;
	}

	bool removed = false;
	auto clearFn = [&](auto &vec) {
		if (auto *item = findItemById(vec, id)) {
			removed = true;
			item->status = ESPTimerStatus::Stopped;
			if (!item->executing) {
				resetItem(*item, type);
			}
		}
	};

	switch (type) {
	case Type::Timeout:
		clearFn(timeouts_);
		break;
	case Type::Interval:
		clearFn(intervals_);
		break;
	case Type::Sec:
		clearFn(secs_);
		break;
	case Type::Ms:
		clearFn(mss_);
		break;
	case Type::Min:
		clearFn(mins_);
		break;
	}

	unlock();
	return removed;
}

ESPTimerStatus ESPTimer::getItemStatus(Type type, uint32_t id) {
	if (!lock()) {
		return ESPTimerStatus::Invalid;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return ESPTimerStatus::Invalid;
	}

	ESPTimerStatus status = ESPTimerStatus::Invalid;
	switch (type) {
	case Type::Timeout:
		if (const auto *item = findItemById(timeouts_, id)) {
			status = item->status;
		}
		break;
	case Type::Interval:
		if (const auto *item = findItemById(intervals_, id)) {
			status = item->status;
		}
		break;
	case Type::Sec:
		if (const auto *item = findItemById(secs_, id)) {
			status = item->status;
		}
		break;
	case Type::Ms:
		if (const auto *item = findItemById(mss_, id)) {
			status = item->status;
		}
		break;
	case Type::Min:
		if (const auto *item = findItemById(mins_, id)) {
			status = item->status;
		}
		break;
	}

	unlock();
	return status;
}

bool ESPTimer::pauseTimer(uint32_t id) {
	return pauseItem(Type::Timeout, id);
}

bool ESPTimer::pauseInterval(uint32_t id) {
	return pauseItem(Type::Interval, id);
}

bool ESPTimer::pauseSecCounter(uint32_t id) {
	return pauseItem(Type::Sec, id);
}

bool ESPTimer::pauseMsCounter(uint32_t id) {
	return pauseItem(Type::Ms, id);
}

bool ESPTimer::pauseMinCounter(uint32_t id) {
	return pauseItem(Type::Min, id);
}

bool ESPTimer::resumeTimer(uint32_t id) {
	return resumeItem(Type::Timeout, id);
}

bool ESPTimer::resumeInterval(uint32_t id) {
	return resumeItem(Type::Interval, id);
}

bool ESPTimer::resumeSecCounter(uint32_t id) {
	return resumeItem(Type::Sec, id);
}

bool ESPTimer::resumeMsCounter(uint32_t id) {
	return resumeItem(Type::Ms, id);
}

bool ESPTimer::resumeMinCounter(uint32_t id) {
	return resumeItem(Type::Min, id);
}

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

bool ESPTimer::clearTimeout(uint32_t id) {
	return clearItem(Type::Timeout, id);
}

bool ESPTimer::clearInterval(uint32_t id) {
	return clearItem(Type::Interval, id);
}

bool ESPTimer::clearSecCounter(uint32_t id) {
	return clearItem(Type::Sec, id);
}

bool ESPTimer::clearMsCounter(uint32_t id) {
	return clearItem(Type::Ms, id);
}

bool ESPTimer::clearMinCounter(uint32_t id) {
	return clearItem(Type::Min, id);
}

ESPTimerStatus ESPTimer::getStatusLocked(uint32_t id) const {
	if (const auto *item = findItemById(timeouts_, id)) {
		return item->status;
	}
	if (const auto *item = findItemById(intervals_, id)) {
		return item->status;
	}
	if (const auto *item = findItemById(secs_, id)) {
		return item->status;
	}
	if (const auto *item = findItemById(mss_, id)) {
		return item->status;
	}
	if (const auto *item = findItemById(mins_, id)) {
		return item->status;
	}
	return ESPTimerStatus::Invalid;
}

ESPTimerStatus ESPTimer::getStatus(uint32_t id) {
	if (!lock()) {
		return ESPTimerStatus::Invalid;
	}
	if (lifecycleState_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
		unlock();
		return ESPTimerStatus::Invalid;
	}

	const ESPTimerStatus status = getStatusLocked(id);
	unlock();
	return status;
}

void ESPTimer::timeoutTaskTrampoline(void *arg) {
	static_cast<ESPTimer *>(arg)->timeoutTask();
}

void ESPTimer::intervalTaskTrampoline(void *arg) {
	static_cast<ESPTimer *>(arg)->intervalTask();
}

void ESPTimer::secTaskTrampoline(void *arg) {
	static_cast<ESPTimer *>(arg)->secTask();
}

void ESPTimer::msTaskTrampoline(void *arg) {
	static_cast<ESPTimer *>(arg)->msTask();
}

void ESPTimer::minTaskTrampoline(void *arg) {
	static_cast<ESPTimer *>(arg)->minTask();
}

void ESPTimer::timeoutTask() {
	while (running_.load(std::memory_order_acquire)) {
		const uint32_t now = millis();
		if (lock()) {
			timeoutDispatch_.clear();
			clearStoppedLocked(timeouts_, Type::Timeout);

			for (size_t index = 0; index < timeouts_.size(); ++index) {
				auto &item = timeouts_[index];
				if (!item.active || item.executing || item.status != ESPTimerStatus::Running) {
					continue;
				}
				if (now >= item.dueAtMs) {
					item.executing = true;
					if (!timerTryPushBack(timeoutDispatch_, TimedDispatch{index})) {
						item.executing = false;
					}
				}
			}

			unlock();
		}

		for (const auto &dispatch : timeoutDispatch_) {
			std::function<void()> *callback = nullptr;
			if (lock()) {
				if (dispatch.index < timeouts_.size()) {
					auto &item = timeouts_[dispatch.index];
					if (item.active && item.executing) {
						callback = &item.cb;
					}
				}
				unlock();
			}

			if (callback) {
				invokeTimerCallback(*callback);
			}

			if (lock()) {
				if (dispatch.index < timeouts_.size()) {
					auto &item = timeouts_[dispatch.index];
					if (item.active && item.executing) {
						item.executing = false;
						if (item.status == ESPTimerStatus::Running) {
							item.status = ESPTimerStatus::Completed;
						}
						if (item.status == ESPTimerStatus::Stopped ||
						    item.status == ESPTimerStatus::Completed) {
							resetItem(item, Type::Timeout);
						}
					}
				}
				unlock();
			}
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}

	if (lock()) {
		markTaskExited(Type::Timeout);
		unlock();
	} else {
		hTimeout_ = nullptr;
	}
	vTaskDelete(nullptr);
}

void ESPTimer::intervalTask() {
	while (running_.load(std::memory_order_acquire)) {
		const uint32_t now = millis();
		if (lock()) {
			intervalDispatch_.clear();
			clearStoppedLocked(intervals_, Type::Interval);

			for (size_t index = 0; index < intervals_.size(); ++index) {
				auto &item = intervals_[index];
				if (!item.active || item.executing || item.status != ESPTimerStatus::Running) {
					continue;
				}
				if (now - item.lastFireMs >= item.periodMs) {
					item.lastFireMs = now;
					item.executing = true;
					if (!timerTryPushBack(intervalDispatch_, TimedDispatch{index})) {
						item.executing = false;
					}
				}
			}

			unlock();
		}

		for (const auto &dispatch : intervalDispatch_) {
			std::function<void()> *callback = nullptr;
			if (lock()) {
				if (dispatch.index < intervals_.size()) {
					auto &item = intervals_[dispatch.index];
					if (item.active && item.executing) {
						callback = &item.cb;
					}
				}
				unlock();
			}

			if (callback) {
				invokeTimerCallback(*callback);
			}

			if (lock()) {
				if (dispatch.index < intervals_.size()) {
					auto &item = intervals_[dispatch.index];
					if (item.active && item.executing) {
						item.executing = false;
						if (item.status == ESPTimerStatus::Stopped ||
						    item.status == ESPTimerStatus::Completed) {
							resetItem(item, Type::Interval);
						}
					}
				}
				unlock();
			}
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}

	if (lock()) {
		markTaskExited(Type::Interval);
		unlock();
	} else {
		hInterval_ = nullptr;
	}
	vTaskDelete(nullptr);
}

void ESPTimer::secTask() {
	while (running_.load(std::memory_order_acquire)) {
		const uint32_t now = millis();
		if (lock()) {
			secDispatch_.clear();
			clearStoppedLocked(secs_, Type::Sec);

			for (size_t index = 0; index < secs_.size(); ++index) {
				auto &item = secs_[index];
				if (!item.active || item.executing || item.status != ESPTimerStatus::Running) {
					continue;
				}
				if (now - item.lastTickMs >= 1000) {
					item.lastTickMs = now;
					int secLeft = 0;
					if (item.endAtMs > now) {
						const uint32_t remaining = item.endAtMs - now;
						secLeft = static_cast<int>((static_cast<uint64_t>(remaining) + 999) / 1000);
					}
					item.executing = true;
					if (!timerTryPushBack(secDispatch_, SecDispatch{index, secLeft})) {
						item.executing = false;
					} else if (now >= item.endAtMs) {
						item.status = ESPTimerStatus::Completed;
					}
				}
			}

			unlock();
		}

		for (const auto &dispatch : secDispatch_) {
			std::function<void(int)> *callback = nullptr;
			if (lock()) {
				if (dispatch.index < secs_.size()) {
					auto &item = secs_[dispatch.index];
					if (item.active && item.executing) {
						callback = &item.cb;
					}
				}
				unlock();
			}

			if (callback) {
				invokeTimerCallback(*callback, dispatch.arg);
			}

			if (lock()) {
				if (dispatch.index < secs_.size()) {
					auto &item = secs_[dispatch.index];
					if (item.active && item.executing) {
						item.executing = false;
						if (item.status == ESPTimerStatus::Stopped ||
						    item.status == ESPTimerStatus::Completed) {
							resetItem(item, Type::Sec);
						}
					}
				}
				unlock();
			}
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}

	if (lock()) {
		markTaskExited(Type::Sec);
		unlock();
	} else {
		hSec_ = nullptr;
	}
	vTaskDelete(nullptr);
}

void ESPTimer::msTask() {
	while (running_.load(std::memory_order_acquire)) {
		const uint32_t now = millis();
		if (lock()) {
			msDispatch_.clear();
			clearStoppedLocked(mss_, Type::Ms);

			for (size_t index = 0; index < mss_.size(); ++index) {
				auto &item = mss_[index];
				if (!item.active || item.executing || item.status != ESPTimerStatus::Running) {
					continue;
				}
				if (now - item.lastTickMs >= 1) {
					item.lastTickMs = now;
					uint32_t msLeft = 0;
					if (item.endAtMs > now) {
						msLeft = item.endAtMs - now;
					}
					item.executing = true;
					if (!timerTryPushBack(msDispatch_, MsDispatch{index, msLeft})) {
						item.executing = false;
					} else if (now >= item.endAtMs) {
						item.status = ESPTimerStatus::Completed;
					}
				}
			}

			unlock();
		}

		for (const auto &dispatch : msDispatch_) {
			std::function<void(uint32_t)> *callback = nullptr;
			if (lock()) {
				if (dispatch.index < mss_.size()) {
					auto &item = mss_[dispatch.index];
					if (item.active && item.executing) {
						callback = &item.cb;
					}
				}
				unlock();
			}

			if (callback) {
				invokeTimerCallback(*callback, dispatch.arg);
			}

			if (lock()) {
				if (dispatch.index < mss_.size()) {
					auto &item = mss_[dispatch.index];
					if (item.active && item.executing) {
						item.executing = false;
						if (item.status == ESPTimerStatus::Stopped ||
						    item.status == ESPTimerStatus::Completed) {
							resetItem(item, Type::Ms);
						}
					}
				}
				unlock();
			}
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}

	if (lock()) {
		markTaskExited(Type::Ms);
		unlock();
	} else {
		hMs_ = nullptr;
	}
	vTaskDelete(nullptr);
}

void ESPTimer::minTask() {
	while (running_.load(std::memory_order_acquire)) {
		const uint32_t now = millis();
		if (lock()) {
			minDispatch_.clear();
			clearStoppedLocked(mins_, Type::Min);

			for (size_t index = 0; index < mins_.size(); ++index) {
				auto &item = mins_[index];
				if (!item.active || item.executing || item.status != ESPTimerStatus::Running) {
					continue;
				}
				if (now - item.lastTickMs >= 60000) {
					item.lastTickMs = now;
					int minLeft = 0;
					if (item.endAtMs > now) {
						const uint32_t remaining = item.endAtMs - now;
						minLeft = static_cast<int>(
						    (static_cast<uint64_t>(remaining) + 60000 - 1) / 60000
						);
					}
					item.executing = true;
					if (!timerTryPushBack(minDispatch_, MinDispatch{index, minLeft})) {
						item.executing = false;
					} else if (now >= item.endAtMs) {
						item.status = ESPTimerStatus::Completed;
					}
				}
			}

			unlock();
		}

		for (const auto &dispatch : minDispatch_) {
			std::function<void(int)> *callback = nullptr;
			if (lock()) {
				if (dispatch.index < mins_.size()) {
					auto &item = mins_[dispatch.index];
					if (item.active && item.executing) {
						callback = &item.cb;
					}
				}
				unlock();
			}

			if (callback) {
				invokeTimerCallback(*callback, dispatch.arg);
			}

			if (lock()) {
				if (dispatch.index < mins_.size()) {
					auto &item = mins_[dispatch.index];
					if (item.active && item.executing) {
						item.executing = false;
						if (item.status == ESPTimerStatus::Stopped ||
						    item.status == ESPTimerStatus::Completed) {
							resetItem(item, Type::Min);
						}
					}
				}
				unlock();
			}
		}

		vTaskDelay(pdMS_TO_TICKS(100));
	}

	if (lock()) {
		markTaskExited(Type::Min);
		unlock();
	} else {
		hMin_ = nullptr;
	}
	vTaskDelete(nullptr);
}
