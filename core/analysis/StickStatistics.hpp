#pragma once

#include "core/model/StickTypes.hpp"

#include <cstdint>
#include <vector>

namespace qss {

struct StickPointSample {
	std::int64_t timestampTicks = 0;
	Vec2 raw{};
	Vec2 output{};
};

struct StickStatistics {
	std::size_t sampleCount = 0;
	Vec2 rawMean{};
	Vec2 rawMinimum{};
	Vec2 rawMaximum{};
	Vec2 rawStandardDeviation{};
	float rawMeanRadius = 0.0F;
	float rawMaximumRadius = 0.0F;
	double updateHz = 0.0;
	bool valid = false;
};

StickStatistics CalculateStickStatistics(
	const std::vector<StickPointSample>& samples,
	std::int64_t qpcFrequency
);

} // namespace qss
