#pragma once

#if __has_include(<ESPBufferManager.h>)
#include <ESPBufferManager.h>
#define ESP_TIMER_HAS_BUFFER_MANAGER 1
#elif __has_include(<esp_buffer_manager/buffer_manager.h>)
#include <esp_buffer_manager/buffer_manager.h>
#define ESP_TIMER_HAS_BUFFER_MANAGER 1
#else
#define ESP_TIMER_HAS_BUFFER_MANAGER 0
#endif

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace timer_allocator_detail {
inline void *allocate(std::size_t bytes, bool usePSRAMBuffers) noexcept {
#if ESP_TIMER_HAS_BUFFER_MANAGER
	return ESPBufferManager::allocate(bytes, usePSRAMBuffers);
#else
	(void)usePSRAMBuffers;
	return std::malloc(bytes);
#endif
}

inline void deallocate(void *ptr) noexcept {
#if ESP_TIMER_HAS_BUFFER_MANAGER
	ESPBufferManager::deallocate(ptr);
#else
	std::free(ptr);
#endif
}
} // namespace timer_allocator_detail

template <typename T> class TimerAllocator {
  public:
	using value_type = T;

	TimerAllocator() noexcept = default;
	explicit TimerAllocator(bool usePSRAMBuffers) noexcept : usePSRAMBuffers_(usePSRAMBuffers) {
	}

	template <typename U>
	TimerAllocator(const TimerAllocator<U> &other) noexcept
	    : usePSRAMBuffers_(other.usePSRAMBuffers()) {
	}

	T *allocate(std::size_t n) {
		if (n == 0) {
			return nullptr;
		}
		if (n > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
			return nullptr;
		}

		void *memory = timer_allocator_detail::allocate(n * sizeof(T), usePSRAMBuffers_);
		if (memory == nullptr) {
			return nullptr;
		}
		return static_cast<T *>(memory);
	}

	void deallocate(T *ptr, std::size_t) noexcept {
		timer_allocator_detail::deallocate(ptr);
	}

	bool usePSRAMBuffers() const noexcept {
		return usePSRAMBuffers_;
	}

	template <typename U> bool operator==(const TimerAllocator<U> &other) const noexcept {
		return usePSRAMBuffers_ == other.usePSRAMBuffers();
	}

	template <typename U> bool operator!=(const TimerAllocator<U> &other) const noexcept {
		return !(*this == other);
	}

  private:
	template <typename> friend class TimerAllocator;

	bool usePSRAMBuffers_ = false;
};

template <typename T> using TimerVector = std::vector<T, TimerAllocator<T>>;

template <typename T>
inline bool timerTryReserve(TimerVector<T> &buffer, std::size_t requiredCapacity) noexcept {
	if (requiredCapacity <= buffer.capacity()) {
		return true;
	}

	const bool usePSRAMBuffers = buffer.get_allocator().usePSRAMBuffers();
	if (requiredCapacity > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
		return false;
	}

	void *probe = timer_allocator_detail::allocate(requiredCapacity * sizeof(T), usePSRAMBuffers);
	if (probe == nullptr) {
		return false;
	}
	timer_allocator_detail::deallocate(probe);

#if defined(__cpp_exceptions)
	try {
		buffer.reserve(requiredCapacity);
	} catch (const std::exception &) {
		return false;
	}
#else
	buffer.reserve(requiredCapacity);
#endif

	return buffer.capacity() >= requiredCapacity;
}

template <typename T>
inline bool
timerTryAssign(TimerVector<T> &buffer, std::size_t count, const T &fillValue = T()) noexcept {
	TimerVector<T> tmp(buffer.get_allocator());
	if (!timerTryReserve(tmp, count)) {
		return false;
	}

#if defined(__cpp_exceptions)
	try {
		tmp.assign(count, fillValue);
	} catch (const std::exception &) {
		return false;
	}
#else
	tmp.assign(count, fillValue);
#endif

	buffer.swap(tmp);
	return true;
}

template <typename T>
inline bool timerTryPushBack(TimerVector<T> &buffer, const T &value) noexcept {
	if (buffer.size() >= buffer.capacity() && !timerTryReserve(buffer, buffer.size() + 1)) {
		return false;
	}

#if defined(__cpp_exceptions)
	try {
		buffer.push_back(value);
	} catch (const std::exception &) {
		return false;
	}
#else
	buffer.push_back(value);
#endif

	return true;
}

template <typename T> inline bool timerTryPushBack(TimerVector<T> &buffer, T &&value) noexcept {
	if (buffer.size() >= buffer.capacity() && !timerTryReserve(buffer, buffer.size() + 1)) {
		return false;
	}

#if defined(__cpp_exceptions)
	try {
		buffer.push_back(std::move(value));
	} catch (const std::exception &) {
		return false;
	}
#else
	buffer.push_back(std::move(value));
#endif

	return true;
}
