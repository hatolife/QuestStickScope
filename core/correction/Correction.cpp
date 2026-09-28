#include "core/correction/Correction.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace qss {
namespace {

constexpr float kMinimumOuterRadius = 0.001F;

bool IsFinite(float value) {
	return std::isfinite(value);
}

Vec2 Sanitize(Vec2 value) {
	if (!IsFinite(value.x)) {
		value.x = 0.0F;
	}
	if (!IsFinite(value.y)) {
		value.y = 0.0F;
	}
	return value;
}

float Length(Vec2 value) {
	return std::sqrt(value.x * value.x + value.y * value.y);
}

Vec2 ScaleToRadius(Vec2 value, float radius) {
	const float length = Length(value);
	if (length <= 0.0F) {
		return {};
	}
	const float scale = radius / length;
	return {value.x * scale, value.y * scale};
}

Vec2 ApplyInnerDeadzone(Vec2 value, float deadzone) {
	deadzone = std::clamp(deadzone, 0.0F, 0.999F);
	const float radius = Length(value);
	if (radius <= deadzone) {
		return {};
	}

	const float remappedRadius = (radius - deadzone) / (1.0F - deadzone);
	return ScaleToRadius(value, remappedRadius);
}

Vec2 ApplyOuterNormalization(Vec2 deadzoned, Vec2 centered, const CorrectionSettings& settings) {
	const float centeredRadius = Length(centered);
	if (centeredRadius <= 0.0F) {
		return {};
	}

	float outerRadius = InterpolateOuterRadius(centered, settings);
	outerRadius = std::max(outerRadius, kMinimumOuterRadius);

	float effectiveOuterRadius = outerRadius;
	if (settings.innerDeadzoneEnabled) {
		const float deadzone = std::clamp(settings.innerDeadzone, 0.0F, 0.999F);
		if (outerRadius <= deadzone) {
			return {};
		}
		effectiveOuterRadius = (outerRadius - deadzone) / (1.0F - deadzone);
	}

	effectiveOuterRadius = std::max(effectiveOuterRadius, kMinimumOuterRadius);
	const float deadzonedRadius = Length(deadzoned);
	return ScaleToRadius(deadzoned, deadzonedRadius / effectiveOuterRadius);
}

Vec2 ClampUnitCircle(Vec2 value) {
	const float radius = Length(value);
	if (radius <= 1.0F) {
		return value;
	}
	return ScaleToRadius(value, 1.0F);
}

} // namespace

CorrectionSettings::CorrectionSettings() {
	outerRadius.fill(1.0F);
}

float InterpolateOuterRadius(Vec2 centeredInput, const CorrectionSettings& settings) {
	centeredInput = Sanitize(centeredInput);
	if (Length(centeredInput) <= 0.0F) {
		return 1.0F;
	}

	float angle = std::atan2(centeredInput.y, centeredInput.x);
	if (angle < 0.0F) {
		angle += 2.0F * std::numbers::pi_v<float>;
	}

	const float scaled = angle * static_cast<float>(kOuterDirectionCount) /
		(2.0F * std::numbers::pi_v<float>);
	const std::size_t index0 = static_cast<std::size_t>(std::floor(scaled)) % kOuterDirectionCount;
	const std::size_t index1 = (index0 + 1) % kOuterDirectionCount;
	const float t = scaled - std::floor(scaled);

	const float radius0 = std::max(settings.outerRadius[index0], kMinimumOuterRadius);
	const float radius1 = std::max(settings.outerRadius[index1], kMinimumOuterRadius);
	return radius0 + (radius1 - radius0) * t;
}

CorrectionResult ApplyCorrection(Vec2 input, const CorrectionSettings& settings) {
	CorrectionResult result{};
	input = Sanitize(input);

	if (!settings.enabled) {
		result.centered = input;
		result.deadzoned = input;
		result.normalized = input;
		result.output = input;
		return result;
	}

	result.centered = input;
	if (settings.centerOffsetEnabled) {
		result.centered.x -= settings.center.x;
		result.centered.y -= settings.center.y;
	}
	result.centered = Sanitize(result.centered);

	result.deadzoned = result.centered;
	if (settings.innerDeadzoneEnabled) {
		result.deadzoned = ApplyInnerDeadzone(result.centered, settings.innerDeadzone);
	}
	result.deadzoned = Sanitize(result.deadzoned);

	result.normalized = result.deadzoned;
	if (settings.outerNormalizationEnabled) {
		result.normalized = ApplyOuterNormalization(result.deadzoned, result.centered, settings);
	}
	result.normalized = Sanitize(result.normalized);

	result.output = result.normalized;
	if (settings.clampEnabled) {
		result.output = ClampUnitCircle(result.output);
	}
	result.output = Sanitize(result.output);

	return result;
}

} // namespace qss
