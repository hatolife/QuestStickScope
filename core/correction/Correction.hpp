#pragma once

#include "core/model/StickTypes.hpp"

#include <array>
#include <cstddef>

namespace qss {

inline constexpr std::size_t kOuterDirectionCount = 64;

struct CorrectionSettings {
	bool enabled = false;
	bool centerOffsetEnabled = true;
	bool innerDeadzoneEnabled = true;
	bool outerNormalizationEnabled = true;
	bool clampEnabled = true;
	Vec2 center{};
	float innerDeadzone = 0.0F;
	std::array<float, kOuterDirectionCount> outerRadius{};

	CorrectionSettings();
};

struct CorrectionResult {
	Vec2 centered{};
	Vec2 deadzoned{};
	Vec2 normalized{};
	Vec2 output{};
};

CorrectionResult ApplyCorrection(Vec2 input, const CorrectionSettings& settings);
float InterpolateOuterRadius(Vec2 centeredInput, const CorrectionSettings& settings);

} // namespace qss
