#include "platform/Clock.hpp"

#ifdef _WIN32
#include <Windows.h>
#else
#include <chrono>
#endif

namespace qss {

std::int64_t MonotonicClock::NowTicks() noexcept {
#ifdef _WIN32
	LARGE_INTEGER counter{};
	if (::QueryPerformanceCounter(&counter) == 0) {
		return 0;
	}
	return counter.QuadPart;
#else
	return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

std::int64_t MonotonicClock::Frequency() noexcept {
#ifdef _WIN32
	LARGE_INTEGER frequency{};
	if (::QueryPerformanceFrequency(&frequency) == 0) {
		return 1;
	}
	return frequency.QuadPart;
#else
	using Period = std::chrono::steady_clock::period;
	return static_cast<std::int64_t>(Period::den / Period::num);
#endif
}

double MonotonicClock::TicksToSeconds(std::int64_t ticks) noexcept {
	const std::int64_t frequency = Frequency();
	if (frequency <= 0) {
		return 0.0;
	}
	return static_cast<double>(ticks) / static_cast<double>(frequency);
}

} // namespace qss
