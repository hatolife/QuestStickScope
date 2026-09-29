#include "core/analysis/StickStatistics.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace {

TEST(StickStatisticsTests, CalculatesBasicStatistics) {
	const std::vector<qss::StickPointSample> samples = {
		{0, {0.0F, 0.0F}, {}},
		{100, {1.0F, -1.0F}, {}},
		{200, {0.0F, 1.0F}, {}},
	};

	const qss::StickStatistics result =
		qss::CalculateStickStatistics(samples, 100);

	ASSERT_TRUE(result.valid);
	EXPECT_EQ(result.sampleCount, 3U);
	EXPECT_NEAR(result.rawMean.x, 1.0F / 3.0F, 0.0001F);
	EXPECT_NEAR(result.rawMean.y, 0.0F, 0.0001F);
	EXPECT_FLOAT_EQ(result.rawMinimum.x, 0.0F);
	EXPECT_FLOAT_EQ(result.rawMinimum.y, -1.0F);
	EXPECT_FLOAT_EQ(result.rawMaximum.x, 1.0F);
	EXPECT_FLOAT_EQ(result.rawMaximum.y, 1.0F);
	EXPECT_NEAR(result.updateHz, 1.0, 0.0001);
}

TEST(StickStatisticsTests, ReportsZeroHzForSingleSample) {
	const std::vector<qss::StickPointSample> samples = {
		{123, {0.2F, -0.3F}, {}},
	};
	const qss::StickStatistics result =
		qss::CalculateStickStatistics(samples, 10000000);

	ASSERT_TRUE(result.valid);
	EXPECT_DOUBLE_EQ(result.updateHz, 0.0);
}

TEST(StickStatisticsTests, RejectsNonFiniteInput) {
	const std::vector<qss::StickPointSample> samples = {
		{0, {0.0F, std::numeric_limits<float>::quiet_NaN()}, {}},
	};
	const qss::StickStatistics result =
		qss::CalculateStickStatistics(samples, 10000000);

	EXPECT_FALSE(result.valid);
}

} // namespace
