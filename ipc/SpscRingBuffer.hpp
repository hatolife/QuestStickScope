#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace qss {

template <typename T, std::size_t Capacity>
class SpscRingBuffer {
	static_assert(Capacity >= 2, "Capacity must be at least 2.");
	static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable.");

public:
	bool TryPush(const T& value) noexcept {
		const std::size_t write = m_write.load(std::memory_order_relaxed);
		const std::size_t next = Increment(write);

		if (next == m_read.load(std::memory_order_acquire)) {
			m_dropped.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		m_values[write] = value;
		m_write.store(next, std::memory_order_release);
		return true;
	}

	bool TryPop(T& value) noexcept {
		const std::size_t read = m_read.load(std::memory_order_relaxed);
		if (read == m_write.load(std::memory_order_acquire)) {
			return false;
		}

		value = m_values[read];
		m_read.store(Increment(read), std::memory_order_release);
		return true;
	}

	std::size_t DroppedCount() const noexcept {
		return m_dropped.load(std::memory_order_relaxed);
	}

	bool Empty() const noexcept {
		return m_read.load(std::memory_order_acquire) == m_write.load(std::memory_order_acquire);
	}

private:
	static constexpr std::size_t Increment(std::size_t value) noexcept {
		return (value + 1) % Capacity;
	}

	std::array<T, Capacity> m_values{};
	alignas(64) std::atomic<std::size_t> m_write{0};
	alignas(64) std::atomic<std::size_t> m_read{0};
	std::atomic<std::size_t> m_dropped{0};
};

} // namespace qss
