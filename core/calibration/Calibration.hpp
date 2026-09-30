#pragma once

#include "core/correction/Correction.hpp"
#include "core/model/StickTypes.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace qss {

struct CenterCalibrationResult {
	Vec2 center{};
	float noiseRadiusP99 = 0.0F;
	float recommendedDeadzone = 0.0F;
	std::array<float, kCorrectionDirectionCount> minimumRadius{};
	std::array<float, kCorrectionDirectionCount> maximumRadius{};
	std::array<float, kCorrectionDirectionCount> innerRadius{};
	std::array<bool, kCorrectionDirectionCount> measured{};
	std::size_t measuredDirectionCount = 0;
	std::size_t sampleCount = 0;
	bool valid = false;
};

struct OuterCalibrationResult {
	std::array<float, kCorrectionDirectionCount> minimumRadius{};
	std::array<float, kCorrectionDirectionCount> maximumRadius{};
	std::array<float, kCorrectionDirectionCount> radius{};
	std::array<bool, kCorrectionDirectionCount> measured{};
	std::size_t measuredDirectionCount = 0;
	bool valid = false;
};

CenterCalibrationResult CalibrateCenter(
	const std::vector<Vec2>& samples,
	std::size_t minimumSamplesPerDirection = 3
);
OuterCalibrationResult CalibrateOuterRange(
	const std::vector<Vec2>& samples,
	Vec2 center,
	std::size_t minimumSamplesPerDirection = 3
);
CorrectionSettings BuildCorrectionSettings(
	const CenterCalibrationResult& center,
	const OuterCalibrationResult& outer
);

} // namespace qss
