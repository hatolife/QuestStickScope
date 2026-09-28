#include "core/calibration/Calibration.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

TEST(CalibrationTests, CenterUsesRobustMedian) {
	std::vector<qss::Vec2> samples;
	for (int index = 0; index < 100; ++index) {
		const float noise = static_cast<float>((index % 5) - 2) * 0.001F;
		samples.push_back({0.08F + noise, -0.04F - noise});
	}
	samples.push_back({0.95F, 0.95F});

	const qss::CenterCalibrationResult result = qss::CalibrateCenter(samples);

	ASSERT_TRUE(result.valid);
	EXPECT_NEAR(result.center.x, 0.08F, 0.002F);
	EXPECT_NEAR(result.center.y, -0.04F, 0.002F);
	EXPECT_LT(result.recommendedDeadzone, 0.02F);
}

TEST(CalibrationTests, OuterRangeMeasuresCircularRadius) {
	std::vector<qss::Vec2> samples;
	for (std::size_t direction = 0; direction < qss::kOuterDirectionCount; ++direction) {
		const float angle = (static_cast<float>(direction) + 0.25F) *
			2.0F * std::numbers::pi_v<float> / static_cast<float>(qss::kOuterDirectionCount);
		for (int repeat = 0; repeat < 5; ++repeat) {
			const float radius = 0.82F + static_cast<float>(repeat - 2) * 0.002F;
			samples.push_back({std::cos(angle) * radius, std::sin(angle) * radius});
		}
	}

	const qss::OuterCalibrationResult result = qss::CalibrateOuterRange(samples, {});

	ASSERT_TRUE(result.valid);
	EXPECT_EQ(result.measuredDirectionCount, qss::kOuterDirectionCount);
	for (const float radius : result.radius) {
		EXPECT_NEAR(radius, 0.824F, 0.005F);
	}
}

TEST(CalibrationTests, MissingDirectionsAreInterpolated) {
	std::vector<qss::Vec2> samples;
	for (std::size_t direction = 0; direction < qss::kOuterDirectionCount; direction += 2) {
		const float angle = (static_cast<float>(direction) + 0.25F) *
			2.0F * std::numbers::pi_v<float> / static_cast<float>(qss::kOuterDirectionCount);
		for (int repeat = 0; repeat < 4; ++repeat) {
			samples.push_back({std::cos(angle) * 0.9F, std::sin(angle) * 0.9F});
		}
	}

	const qss::OuterCalibrationResult result = qss::CalibrateOuterRange(samples, {});

	EXPECT_FALSE(result.valid);
	EXPECT_EQ(result.measuredDirectionCount, qss::kOuterDirectionCount / 2);
	for (const float radius : result.radius) {
		EXPECT_NEAR(radius, 0.9F, 0.01F);
	}
}

} // namespace
