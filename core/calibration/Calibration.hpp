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
	std::size_t sampleCount = 0;
	bool valid = false;
};

struct OuterCalibrationResult {
	std::array<float, kOuterDirectionCount> radius{};
	std::array<bool, kOuterDirectionCount> measured{};
	std::size_t measuredDirectionCount = 0;
	bool valid = false;
};

CenterCalibrationResult CalibrateCenter(const std::vector<Vec2>& samples);
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
