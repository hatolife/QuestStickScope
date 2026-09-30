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

TEST(CalibrationTests, CenterBuildsOneDegreeInnerRange) {
	std::vector<qss::Vec2> samples;
	for (std::size_t direction = 0; direction < qss::kCorrectionDirectionCount; ++direction) {
		const float angle =
			(static_cast<float>(direction) + 0.25F) *
			2.0F * std::numbers::pi_v<float> /
			static_cast<float>(qss::kCorrectionDirectionCount);
		for (int repeat = 0; repeat < 4; ++repeat) {
			const float radius = 0.02F + static_cast<float>(repeat) * 0.001F;
			samples.push_back({
				0.08F + std::cos(angle) * radius,
				-0.04F + std::sin(angle) * radius,
			});
		}
	}

	const qss::CenterCalibrationResult result = qss::CalibrateCenter(samples);

	ASSERT_TRUE(result.valid);
	EXPECT_EQ(result.measuredDirectionCount, qss::kCorrectionDirectionCount);
	EXPECT_GT(result.innerRadius[0], 0.02F);
	EXPECT_LT(result.innerRadius[0], 0.04F);
	EXPECT_LE(result.minimumRadius[0], result.maximumRadius[0]);
}

TEST(CalibrationTests, OuterRangeMeasuresCircularRadiusPerDegree) {
	std::vector<qss::Vec2> samples;
	for (std::size_t direction = 0; direction < qss::kCorrectionDirectionCount; ++direction) {
		const float angle =
			(static_cast<float>(direction) + 0.25F) *
			2.0F * std::numbers::pi_v<float> /
			static_cast<float>(qss::kCorrectionDirectionCount);
		for (int repeat = 0; repeat < 5; ++repeat) {
			const float radius = 0.82F + static_cast<float>(repeat - 2) * 0.002F;
			samples.push_back({std::cos(angle) * radius, std::sin(angle) * radius});
		}
	}

	const qss::OuterCalibrationResult result =
		qss::CalibrateOuterRange(samples, {});

	ASSERT_TRUE(result.valid);
	EXPECT_EQ(result.measuredDirectionCount, qss::kCorrectionDirectionCount);
	for (const float radius : result.radius) {
		EXPECT_NEAR(radius, 0.824F, 0.005F);
	}
}

TEST(CalibrationTests, MissingDirectionsAreInterpolated) {
	std::vector<qss::Vec2> samples;
	for (std::size_t direction = 0;
		direction < qss::kCorrectionDirectionCount;
		direction += 10) {
		const float angle =
			(static_cast<float>(direction) + 0.25F) *
			2.0F * std::numbers::pi_v<float> /
			static_cast<float>(qss::kCorrectionDirectionCount);
		for (int repeat = 0; repeat < 4; ++repeat) {
			samples.push_back({std::cos(angle) * 0.9F, std::sin(angle) * 0.9F});
		}
	}

	const qss::OuterCalibrationResult result =
		qss::CalibrateOuterRange(samples, {});

	EXPECT_FALSE(result.valid);
	EXPECT_GT(result.measuredDirectionCount, 0U);
	EXPECT_LT(result.measuredDirectionCount, qss::kCorrectionDirectionCount * 3 / 4);
	for (const float radius : result.radius) {
		EXPECT_NEAR(radius, 0.9F, 0.01F);
	}
}

TEST(CalibrationTests, BuildSettingsCombinesInnerAndOuterRanges) {
	qss::CenterCalibrationResult center;
	center.valid = true;
	center.center = {0.03F, -0.02F};
	center.innerRadius.fill(0.08F);

	qss::OuterCalibrationResult outer;
	outer.valid = true;
	outer.measuredDirectionCount = qss::kCorrectionDirectionCount;
	outer.radius.fill(0.85F);

	const qss::CorrectionSettings settings =
		qss::BuildCorrectionSettings(center, outer);

	EXPECT_FLOAT_EQ(settings.center.x, 0.03F);
	EXPECT_FLOAT_EQ(settings.center.y, -0.02F);
	EXPECT_FLOAT_EQ(settings.innerRadius[42], 0.08F);
	EXPECT_FLOAT_EQ(settings.outerRadius[42], 0.85F);
	EXPECT_FLOAT_EQ(settings.innerDeadzone, 0.0F);
}

} // namespace
