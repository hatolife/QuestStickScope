#include "core/calibration/Calibration.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace qss {
namespace {

constexpr std::size_t kAngularWindowDegrees = 2;

float Percentile(std::vector<float> values, float percentile) {
	if (values.empty()) {
		return 0.0F;
	}
	percentile = std::clamp(percentile, 0.0F, 1.0F);
	const std::size_t index = static_cast<std::size_t>(
		std::round(percentile * static_cast<float>(values.size() - 1))
	);
	std::nth_element(values.begin(), values.begin() + index, values.end());
	return values[index];
}

float Median(std::vector<float> values) {
	return Percentile(std::move(values), 0.5F);
}

float Radius(Vec2 value) {
	return std::sqrt(value.x * value.x + value.y * value.y);
}

std::size_t DirectionIndex(Vec2 value) {
	float angle = std::atan2(value.y, value.x);
	if (angle < 0.0F) {
		angle += 2.0F * std::numbers::pi_v<float>;
	}
	const float scaled = angle * static_cast<float>(kCorrectionDirectionCount) /
		(2.0F * std::numbers::pi_v<float>);
	return static_cast<std::size_t>(std::floor(scaled)) % kCorrectionDirectionCount;
}

std::vector<float> GatherAngularWindow(
	const std::array<std::vector<float>, kCorrectionDirectionCount>& bins,
	std::size_t direction
) {
	std::vector<float> values;
	for (int offset = -static_cast<int>(kAngularWindowDegrees);
		offset <= static_cast<int>(kAngularWindowDegrees);
		++offset) {
		const std::size_t index = (
			direction +
			kCorrectionDirectionCount +
			static_cast<std::size_t>(
				offset + static_cast<int>(kCorrectionDirectionCount)
			)
		) % kCorrectionDirectionCount;
		values.insert(values.end(), bins[index].begin(), bins[index].end());
	}
	return values;
}

template <typename Array>
void FillMissingCircular(
	Array& values,
	const std::array<bool, kCorrectionDirectionCount>& measured,
	float fallback
) {
	std::size_t measuredCount = 0;
	for (const bool value : measured) {
		if (value) {
			++measuredCount;
		}
	}
	if (measuredCount == 0) {
		values.fill(fallback);
		return;
	}

	for (std::size_t index = 0; index < kCorrectionDirectionCount; ++index) {
		if (measured[index]) {
			continue;
		}

		std::size_t previous = index;
		std::size_t previousDistance = 0;
		for (std::size_t distance = 1; distance < kCorrectionDirectionCount; ++distance) {
			const std::size_t candidate =
				(index + kCorrectionDirectionCount - distance) %
				kCorrectionDirectionCount;
			if (measured[candidate]) {
				previous = candidate;
				previousDistance = distance;
				break;
			}
		}

		std::size_t next = index;
		std::size_t nextDistance = 0;
		for (std::size_t distance = 1; distance < kCorrectionDirectionCount; ++distance) {
			const std::size_t candidate =
				(index + distance) % kCorrectionDirectionCount;
			if (measured[candidate]) {
				next = candidate;
				nextDistance = distance;
				break;
			}
		}

		if (previousDistance == 0 && nextDistance == 0) {
			values[index] = fallback;
		} else if (previousDistance == 0) {
			values[index] = values[next];
		} else if (nextDistance == 0) {
			values[index] = values[previous];
		} else {
			const float t = static_cast<float>(previousDistance) /
				static_cast<float>(previousDistance + nextDistance);
			values[index] =
				values[previous] + (values[next] - values[previous]) * t;
		}
	}
}

void AnalyzeInnerDirections(
	const std::array<std::vector<float>, kCorrectionDirectionCount>& bins,
	std::size_t minimumSamplesPerDirection,
	CenterCalibrationResult& result
) {
	for (std::size_t direction = 0; direction < kCorrectionDirectionCount; ++direction) {
		std::vector<float> values = GatherAngularWindow(bins, direction);
		if (values.size() < minimumSamplesPerDirection) {
			continue;
		}

		const auto [minimum, maximum] =
			std::minmax_element(values.begin(), values.end());
		result.minimumRadius[direction] = *minimum;
		result.maximumRadius[direction] = *maximum;
		result.innerRadius[direction] = std::clamp(
			Percentile(values, 0.99F) * 1.10F,
			0.0F,
			0.50F
		);
		result.measured[direction] = true;
		++result.measuredDirectionCount;
	}

	FillMissingCircular(
		result.minimumRadius,
		result.measured,
		0.0F
	);
	FillMissingCircular(
		result.maximumRadius,
		result.measured,
		result.recommendedDeadzone
	);
	FillMissingCircular(
		result.innerRadius,
		result.measured,
		result.recommendedDeadzone
	);
}

void AnalyzeOuterDirections(
	const std::array<std::vector<float>, kCorrectionDirectionCount>& bins,
	std::size_t minimumSamplesPerDirection,
	OuterCalibrationResult& result
) {
	for (std::size_t direction = 0; direction < kCorrectionDirectionCount; ++direction) {
		std::vector<float> values = GatherAngularWindow(bins, direction);
		if (values.size() < minimumSamplesPerDirection) {
			continue;
		}

		const auto [minimum, maximum] =
			std::minmax_element(values.begin(), values.end());
		result.minimumRadius[direction] = *minimum;
		result.maximumRadius[direction] = *maximum;
		result.radius[direction] = std::max(
			Percentile(values, 0.95F),
			0.001F
		);
		result.measured[direction] = true;
		++result.measuredDirectionCount;
	}

	FillMissingCircular(result.minimumRadius, result.measured, 1.0F);
	FillMissingCircular(result.maximumRadius, result.measured, 1.0F);
	FillMissingCircular(result.radius, result.measured, 1.0F);
	result.valid =
		result.measuredDirectionCount >= kCorrectionDirectionCount * 3 / 4;
}

} // namespace

CenterCalibrationResult CalibrateCenter(
	const std::vector<Vec2>& samples,
	std::size_t minimumSamplesPerDirection
) {
	CenterCalibrationResult result;
	result.sampleCount = samples.size();
	if (samples.size() < 8) {
		return result;
	}

	std::vector<float> xs;
	std::vector<float> ys;
	xs.reserve(samples.size());
	ys.reserve(samples.size());
	for (const Vec2 sample : samples) {
		if (!std::isfinite(sample.x) || !std::isfinite(sample.y)) {
			continue;
		}
		xs.push_back(sample.x);
		ys.push_back(sample.y);
	}
	if (xs.size() < 8) {
		return result;
	}

	result.center = {Median(xs), Median(ys)};
	std::vector<float> radii;
	radii.reserve(xs.size());
	std::array<std::vector<float>, kCorrectionDirectionCount> bins;
	for (const Vec2 sample : samples) {
		if (!std::isfinite(sample.x) || !std::isfinite(sample.y)) {
			continue;
		}
		const Vec2 centered{
			sample.x - result.center.x,
			sample.y - result.center.y,
		};
		const float radius = Radius(centered);
		radii.push_back(radius);
		if (radius > std::numeric_limits<float>::epsilon()) {
			bins[DirectionIndex(centered)].push_back(radius);
		}
	}

	result.noiseRadiusP99 = Percentile(radii, 0.99F);
	result.recommendedDeadzone = std::clamp(
		result.noiseRadiusP99 * 1.25F,
		0.0F,
		0.30F
	);
	AnalyzeInnerDirections(bins, minimumSamplesPerDirection, result);
	result.valid = true;
	return result;
}

OuterCalibrationResult CalibrateOuterRange(
	const std::vector<Vec2>& samples,
	Vec2 center,
	std::size_t minimumSamplesPerDirection
) {
	OuterCalibrationResult result;
	result.minimumRadius.fill(1.0F);
	result.maximumRadius.fill(1.0F);
	result.radius.fill(1.0F);
	std::array<std::vector<float>, kCorrectionDirectionCount> bins;

	for (const Vec2 sample : samples) {
		if (!std::isfinite(sample.x) || !std::isfinite(sample.y)) {
			continue;
		}
		const Vec2 centered{sample.x - center.x, sample.y - center.y};
		const float radius = Radius(centered);
		if (radius <= 0.0F) {
			continue;
		}
		bins[DirectionIndex(centered)].push_back(radius);
	}

	AnalyzeOuterDirections(bins, minimumSamplesPerDirection, result);
	return result;
}

CorrectionSettings BuildCorrectionSettings(
	const CenterCalibrationResult& center,
	const OuterCalibrationResult& outer
) {
	CorrectionSettings settings;
	settings.enabled = false;
	if (center.valid) {
		settings.center = center.center;
		settings.innerDeadzone = 0.0F;
		settings.innerRadius = center.innerRadius;
	}
	if (outer.measuredDirectionCount > 0) {
		settings.outerRadius = outer.radius;
	}
	for (std::size_t direction = 0; direction < kCorrectionDirectionCount; ++direction) {
		settings.outerRadius[direction] = std::max(
			settings.outerRadius[direction],
			settings.innerRadius[direction] + 0.05F
		);
	}
	return settings;
}

} // namespace qss
