#pragma once

#include "core/model/StickTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace qss {

inline constexpr std::size_t kCorrectionDirectionCount = 360;
inline constexpr std::size_t kOuterDirectionCount = kCorrectionDirectionCount;

struct CorrectionSettings {
	bool enabled = false;
	bool centerOffsetEnabled = true;
	bool innerDeadzoneEnabled = true;
	bool outerNormalizationEnabled = true;
	bool clampEnabled = true;
	Vec2 center{};
	float innerDeadzone = 0.0F;
	float outerScale = 1.0F;
	float responseCurve = 0.0F;
	float smoothing = 0.0F;
	std::array<float, kCorrectionDirectionCount> innerRadius{};
	std::array<float, kCorrectionDirectionCount> outerRadius{};

	CorrectionSettings();
};

struct CorrectionResult {
	Vec2 centered{};
	Vec2 deadzoned{};
	Vec2 normalized{};
	Vec2 curved{};
	Vec2 output{};
};

CorrectionResult ApplyCorrection(Vec2 input, const CorrectionSettings& settings);
float InterpolateInnerRadius(Vec2 centeredInput, const CorrectionSettings& settings);
float InterpolateOuterRadius(Vec2 centeredInput, const CorrectionSettings& settings);
float EvaluateResponseCurve(float magnitude, float responseCurve);
float ApplySmoothing(float input, float previousOutput, float smoothing, std::int64_t deltaTicks, std::int64_t tickFrequency);

} // namespace qss
