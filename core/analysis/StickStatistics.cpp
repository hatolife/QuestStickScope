#include "core/analysis/StickStatistics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace qss {

StickStatistics CalculateStickStatistics(
	const std::vector<StickPointSample>& samples,
	std::int64_t qpcFrequency
) {
	StickStatistics result;
	if (samples.empty()) {
		return result;
	}

	result.sampleCount = samples.size();
	result.rawMinimum = {
		std::numeric_limits<float>::max(),
		std::numeric_limits<float>::max(),
	};
	result.rawMaximum = {
		std::numeric_limits<float>::lowest(),
		std::numeric_limits<float>::lowest(),
	};

	double sumX = 0.0;
	double sumY = 0.0;
	double sumRadius = 0.0;
	for (const StickPointSample& sample : samples) {
		if (!std::isfinite(sample.raw.x) || !std::isfinite(sample.raw.y)) {
			return {};
		}
		sumX += sample.raw.x;
		sumY += sample.raw.y;
		const float radius = std::sqrt(
			sample.raw.x * sample.raw.x +
			sample.raw.y * sample.raw.y
		);
		sumRadius += radius;
		result.rawMaximumRadius = std::max(result.rawMaximumRadius, radius);
		result.rawMinimum.x = std::min(result.rawMinimum.x, sample.raw.x);
		result.rawMinimum.y = std::min(result.rawMinimum.y, sample.raw.y);
		result.rawMaximum.x = std::max(result.rawMaximum.x, sample.raw.x);
		result.rawMaximum.y = std::max(result.rawMaximum.y, sample.raw.y);
	}

	const double count = static_cast<double>(samples.size());
	result.rawMean = {
		static_cast<float>(sumX / count),
		static_cast<float>(sumY / count),
	};
	result.rawMeanRadius = static_cast<float>(sumRadius / count);

	double varianceX = 0.0;
	double varianceY = 0.0;
	for (const StickPointSample& sample : samples) {
		const double dx = static_cast<double>(sample.raw.x - result.rawMean.x);
		const double dy = static_cast<double>(sample.raw.y - result.rawMean.y);
		varianceX += dx * dx;
		varianceY += dy * dy;
	}
	result.rawStandardDeviation = {
		static_cast<float>(std::sqrt(varianceX / count)),
		static_cast<float>(std::sqrt(varianceY / count)),
	};

	if (samples.size() >= 2 && qpcFrequency > 0) {
		const std::int64_t elapsedTicks =
			samples.back().timestampTicks - samples.front().timestampTicks;
		if (elapsedTicks > 0) {
			const double elapsedSeconds =
				static_cast<double>(elapsedTicks) / static_cast<double>(qpcFrequency);
			result.updateHz =
				static_cast<double>(samples.size() - 1) / elapsedSeconds;
		}
	}

	result.valid = true;
	return result;
}

} // namespace qss
