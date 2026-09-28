#include "core/calibration/Calibration.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace qss {
namespace {

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
	const float scaled = angle * static_cast<float>(kOuterDirectionCount) /
		(2.0F * std::numbers::pi_v<float>);
	return static_cast<std::size_t>(std::floor(scaled)) % kOuterDirectionCount;
}

void FillMissingDirections(OuterCalibrationResult& result) {
	if (result.measuredDirectionCount == 0) {
		result.radius.fill(1.0F);
		return;
	}

	for (std::size_t index = 0; index < kOuterDirectionCount; ++index) {
		if (result.measured[index]) {
			continue;
		}

		std::size_t previous = index;
		std::size_t previousDistance = 0;
		for (std::size_t distance = 1; distance < kOuterDirectionCount; ++distance) {
			const std::size_t candidate = (index + kOuterDirectionCount - distance) % kOuterDirectionCount;
			if (result.measured[candidate]) {
				previous = candidate;
				previousDistance = distance;
				break;
			}
		}

		std::size_t next = index;
		std::size_t nextDistance = 0;
		for (std::size_t distance = 1; distance < kOuterDirectionCount; ++distance) {
			const std::size_t candidate = (index + distance) % kOuterDirectionCount;
			if (result.measured[candidate]) {
				next = candidate;
				nextDistance = distance;
				break;
			}
		}

		if (previousDistance == 0 && nextDistance == 0) {
			result.radius[index] = 1.0F;
			continue;
		}
		if (previousDistance == 0) {
			result.radius[index] = result.radius[next];
			continue;
		}
		if (nextDistance == 0) {
			result.radius[index] = result.radius[previous];
			continue;
		}

		const float t = static_cast<float>(previousDistance) /
			static_cast<float>(previousDistance + nextDistance);
		result.radius[index] = result.radius[previous] +
			(result.radius[next] - result.radius[previous]) * t;
	}
}

} // namespace

CenterCalibrationResult CalibrateCenter(const std::vector<Vec2>& samples) {
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
	for (const Vec2 sample : samples) {
		if (!std::isfinite(sample.x) || !std::isfinite(sample.y)) {
			continue;
		}
		radii.push_back(Radius({sample.x - result.center.x, sample.y - result.center.y}));
	}

	result.noiseRadiusP99 = Percentile(std::move(radii), 0.99F);
	result.recommendedDeadzone = std::clamp(result.noiseRadiusP99 * 1.25F, 0.0F, 0.30F);
	result.valid = true;
	return result;
}

OuterCalibrationResult CalibrateOuterRange(
	const std::vector<Vec2>& samples,
	Vec2 center,
	std::size_t minimumSamplesPerDirection
) {
	OuterCalibrationResult result;
	result.radius.fill(1.0F);
	std::array<std::vector<float>, kOuterDirectionCount> bins;

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

	for (std::size_t index = 0; index < kOuterDirectionCount; ++index) {
		if (bins[index].size() < minimumSamplesPerDirection) {
			continue;
		}
		result.radius[index] = std::max(Percentile(bins[index], 0.95F), 0.001F);
		result.measured[index] = true;
		++result.measuredDirectionCount;
	}

	FillMissingDirections(result);
	result.valid = result.measuredDirectionCount >= kOuterDirectionCount * 3 / 4;
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
		settings.innerDeadzone = center.recommendedDeadzone;
	}
	if (outer.measuredDirectionCount > 0) {
		settings.outerRadius = outer.radius;
	}
	return settings;
}

} // namespace qss
