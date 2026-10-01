#include "core/correction/Correction.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

namespace {

constexpr float kTolerance = 0.0001F;

TEST(CorrectionTests, DisabledPassesThroughInput) {
	qss::CorrectionSettings settings;
	settings.enabled = false;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.25F, -0.5F}, settings);

	EXPECT_NEAR(result.output.x, 0.25F, kTolerance);
	EXPECT_NEAR(result.output.y, -0.5F, kTolerance);
}

TEST(CorrectionTests, CenterOffsetMovesMeasuredCenterToZero) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.center = {0.12F, -0.08F};
	settings.innerDeadzoneEnabled = false;
	settings.outerNormalizationEnabled = false;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.12F, -0.08F}, settings);

	EXPECT_NEAR(result.output.x, 0.0F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

TEST(CorrectionTests, InnerDeadzoneSuppressesSmallRadialInput) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerDeadzone = 0.1F;
	settings.outerNormalizationEnabled = false;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.06F, 0.06F}, settings);

	EXPECT_NEAR(result.output.x, 0.0F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

TEST(CorrectionTests, InnerDeadzoneRescalesRemainingRange) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerDeadzone = 0.2F;
	settings.outerNormalizationEnabled = false;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.6F, 0.0F}, settings);

	EXPECT_NEAR(result.output.x, 0.5F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

TEST(CorrectionTests, DirectionalInnerDeadzoneUsesCalibratedRadius) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerRadius.fill(0.0F);
	settings.innerRadius[0] = 0.2F;
	settings.outerNormalizationEnabled = false;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.15F, 0.0F}, settings);

	EXPECT_NEAR(result.output.x, 0.0F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

TEST(CorrectionTests, DirectionalRangeMapsInnerToZeroAndOuterToOne) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerRadius.fill(0.0F);
	settings.outerRadius.fill(1.0F);
	settings.innerRadius[0] = 0.2F;
	settings.outerRadius[0] = 0.8F;

	const qss::CorrectionResult middle = qss::ApplyCorrection({0.5F, 0.0F}, settings);
	const qss::CorrectionResult outer = qss::ApplyCorrection({0.8F, 0.0F}, settings);

	EXPECT_NEAR(middle.output.x, 0.5F, kTolerance);
	EXPECT_NEAR(outer.output.x, 1.0F, kTolerance);
}

TEST(CorrectionTests, DirectionalOuterNormalizationUsesCalibratedRadius) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerDeadzoneEnabled = false;
	settings.outerRadius.fill(1.0F);
	settings.outerRadius[0] = 0.8F;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.8F, 0.0F}, settings);

	EXPECT_NEAR(result.output.x, 1.0F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

TEST(CorrectionTests, OuterRadiusInterpolationWrapsAcrossZeroAngle) {
	qss::CorrectionSettings settings;
	settings.outerRadius.fill(1.0F);
	settings.outerRadius[359] = 0.8F;
	settings.outerRadius[0] = 1.0F;

	const float angle = -std::numbers::pi_v<float> / 360.0F;
	const qss::Vec2 input{std::cos(angle), std::sin(angle)};
	const float radius = qss::InterpolateOuterRadius(input, settings);

	EXPECT_NEAR(radius, 0.9F, 0.01F);
}

TEST(CorrectionTests, OuterScaleChangesMaximumZone) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerDeadzoneEnabled = false;
	settings.outerRadius.fill(1.0F);
	settings.outerScale = 0.8F;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.8F, 0.0F}, settings);

	EXPECT_NEAR(result.output.x, 1.0F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

TEST(CorrectionTests, PositiveResponseCurveIncreasesMidRange) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerDeadzoneEnabled = false;
	settings.outerNormalizationEnabled = false;
	settings.responseCurve = 1.0F;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.25F, 0.0F}, settings);

	EXPECT_NEAR(result.output.x, 0.5F, kTolerance);
}

TEST(CorrectionTests, NegativeResponseCurveReducesMidRange) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerDeadzoneEnabled = false;
	settings.outerNormalizationEnabled = false;
	settings.responseCurve = -1.0F;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.5F, 0.0F}, settings);

	EXPECT_NEAR(result.output.x, 0.25F, kTolerance);
}

TEST(CorrectionTests, SmoothingMovesTowardInputWithoutOvershoot) {
	const float output = qss::ApplySmoothing(1.0F, 0.0F, 1.0F, 10, 1000);

	EXPECT_GT(output, 0.0F);
	EXPECT_LT(output, 1.0F);
	EXPECT_FLOAT_EQ(qss::ApplySmoothing(1.0F, 0.0F, 0.0F, 10, 1000), 1.0F);
}

TEST(CorrectionTests, ClampLimitsOutputToUnitCircle) {
	qss::CorrectionSettings settings;
	settings.enabled = true;
	settings.centerOffsetEnabled = false;
	settings.innerDeadzoneEnabled = false;
	settings.outerNormalizationEnabled = false;

	const qss::CorrectionResult result = qss::ApplyCorrection({2.0F, 0.0F}, settings);

	EXPECT_NEAR(result.output.x, 1.0F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

TEST(CorrectionTests, NonFiniteInputNeverEscapesPipeline) {
	qss::CorrectionSettings settings;
	settings.enabled = true;

	const qss::CorrectionResult result = qss::ApplyCorrection(
		{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()},
		settings
	);

	EXPECT_TRUE(std::isfinite(result.output.x));
	EXPECT_TRUE(std::isfinite(result.output.y));
	EXPECT_NEAR(result.output.x, 0.0F, kTolerance);
	EXPECT_NEAR(result.output.y, 0.0F, kTolerance);
}

} // namespace
