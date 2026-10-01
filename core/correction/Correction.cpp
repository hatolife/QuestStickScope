#include "core/correction/Correction.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace qss {
namespace {

constexpr float kMinimumRange = 0.001F;

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

float InterpolateRadius(
	Vec2 centeredInput,
	const std::array<float, kCorrectionDirectionCount>& radius,
	float minimum
) {
	centeredInput = Sanitize(centeredInput);
	if (Length(centeredInput) <= 0.0F) {
		return minimum;
	}

	float angle = std::atan2(centeredInput.y, centeredInput.x);
	if (angle < 0.0F) {
		angle += 2.0F * std::numbers::pi_v<float>;
	}

	const float scaled = angle * static_cast<float>(kCorrectionDirectionCount) /
		(2.0F * std::numbers::pi_v<float>);
	const std::size_t index0 =
		static_cast<std::size_t>(std::floor(scaled)) % kCorrectionDirectionCount;
	const std::size_t index1 = (index0 + 1) % kCorrectionDirectionCount;
	const float t = scaled - std::floor(scaled);

	const float radius0 = std::max(radius[index0], minimum);
	const float radius1 = std::max(radius[index1], minimum);
	return radius0 + (radius1 - radius0) * t;
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

Vec2 ApplyOuterNormalization(
	Vec2 deadzoned,
	Vec2 centered,
	float innerRadius,
	const CorrectionSettings& settings
) {
	if (Length(centered) <= 0.0F) {
		return {};
	}
	const float outerScale = std::clamp(settings.outerScale, 0.25F, 2.0F);
	const float outerRadius = std::max(
		InterpolateOuterRadius(centered, settings) * outerScale,
		kMinimumRange
	);
	if (outerRadius <= innerRadius + kMinimumRange) {
		return {};
	}

	float effectiveOuterRadius = outerRadius;
	if (settings.innerDeadzoneEnabled) {
		const float inner = std::clamp(innerRadius, 0.0F, 0.999F);
		effectiveOuterRadius = (outerRadius - inner) / (1.0F - inner);
	}
	effectiveOuterRadius = std::max(effectiveOuterRadius, kMinimumRange);
	return ScaleToRadius(deadzoned, Length(deadzoned) / effectiveOuterRadius);
}

Vec2 ApplyResponseCurveToVector(Vec2 value, float responseCurve) {
	const float radius = Length(value);
	if (radius <= 0.0F) {
		return {};
	}
	return ScaleToRadius(value, EvaluateResponseCurve(radius, responseCurve));
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
	innerRadius.fill(0.0F);
	outerRadius.fill(1.0F);
}

float EvaluateResponseCurve(float magnitude, float responseCurve) {
	if (!std::isfinite(magnitude)) {
		return 0.0F;
	}
	magnitude = std::max(magnitude, 0.0F);
	responseCurve = std::clamp(responseCurve, -1.0F, 1.0F);
	const float exponent = std::exp2(-responseCurve);
	return std::pow(magnitude, exponent);
}

float ApplySmoothing(float input, float previousOutput, float smoothing, std::int64_t deltaTicks, std::int64_t tickFrequency) {
	input = IsFinite(input) ? input : 0.0F;
	previousOutput = IsFinite(previousOutput) ? previousOutput : input;
	smoothing = std::clamp(smoothing, 0.0F, 1.0F);
	if (smoothing <= 0.0F || deltaTicks <= 0 || tickFrequency <= 0) {
		return input;
	}
	const double deltaSeconds = static_cast<double>(deltaTicks) / static_cast<double>(tickFrequency);
	if (deltaSeconds <= 0.0 || deltaSeconds > 0.5) {
		return input;
	}
	const double amount = static_cast<double>(smoothing);
	const double timeConstantSeconds = 0.002 + 0.148 * amount * amount;
	const double alpha = 1.0 - std::exp(-deltaSeconds / timeConstantSeconds);
	const float output = previousOutput + (input - previousOutput) * static_cast<float>(alpha);
	return IsFinite(output) ? output : input;
}

float InterpolateInnerRadius(Vec2 centeredInput, const CorrectionSettings& settings) {
	return InterpolateRadius(centeredInput, settings.innerRadius, 0.0F);
}

float InterpolateOuterRadius(Vec2 centeredInput, const CorrectionSettings& settings) {
	return InterpolateRadius(centeredInput, settings.outerRadius, kMinimumRange);
}

CorrectionResult ApplyCorrection(Vec2 input, const CorrectionSettings& settings) {
	CorrectionResult result{};
	input = Sanitize(input);

	if (!settings.enabled) {
		result.centered = input;
		result.deadzoned = input;
		result.normalized = input;
		result.curved = input;
		result.output = input;
		return result;
	}

	result.centered = input;
	if (settings.centerOffsetEnabled) {
		result.centered.x -= settings.center.x;
		result.centered.y -= settings.center.y;
	}
	result.centered = Sanitize(result.centered);

	float innerRadius = 0.0F;
	if (settings.innerDeadzoneEnabled) {
		innerRadius = std::max(
			std::clamp(settings.innerDeadzone, 0.0F, 0.999F),
			InterpolateInnerRadius(result.centered, settings)
		);
		innerRadius = std::clamp(innerRadius, 0.0F, 0.999F);
	}

	result.deadzoned = result.centered;
	if (settings.innerDeadzoneEnabled) {
		result.deadzoned = ApplyInnerDeadzone(result.centered, innerRadius);
	}
	result.deadzoned = Sanitize(result.deadzoned);

	result.normalized = result.deadzoned;
	if (settings.outerNormalizationEnabled) {
		result.normalized = ApplyOuterNormalization(
			result.deadzoned,
			result.centered,
			innerRadius,
			settings
		);
	}
	result.normalized = Sanitize(result.normalized);

	result.curved = ApplyResponseCurveToVector(result.normalized, settings.responseCurve);
	result.curved = Sanitize(result.curved);

	result.output = result.curved;
	if (settings.clampEnabled) {
		result.output = ClampUnitCircle(result.output);
	}
	result.output = Sanitize(result.output);
	return result;
}

} // namespace qss
