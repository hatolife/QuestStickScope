#pragma once

#include <cstdint>

namespace qss {

class MonotonicClock {
public:
	static std::int64_t NowTicks() noexcept;
	static std::int64_t Frequency() noexcept;
	static double TicksToSeconds(std::int64_t ticks) noexcept;
};

} // namespace qss
