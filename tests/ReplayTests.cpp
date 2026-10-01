#include "core/recording/Replay.hpp"

#include <gtest/gtest.h>

namespace {

qss::ScalarComponentSnapshot MakeComponent(
	qss::ControllerHand hand,
	qss::ScalarSemantic semantic
) {
	qss::ScalarComponentSnapshot component;
	component.hand = hand;
	component.semantic = semantic;
	return component;
}

TEST(ReplayTests, AppliesCorrectionUsingLatestPairedAxis) {
	qss::RecordingData recording;
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Left,
		qss::ScalarSemantic::JoystickX
	));
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Left,
		qss::ScalarSemantic::JoystickY
	));

	recording.samples.push_back({100, 1, 0, 0.4F, 0.4F, 0.0, 0});
	recording.samples.push_back({101, 2, 1, 0.3F, 0.3F, 0.0, 0});

	qss::ReplayCorrectionSettings settings;
	settings.left.enabled = true;
	settings.left.centerOffsetEnabled = false;
	settings.left.innerDeadzoneEnabled = false;
	settings.left.outerNormalizationEnabled = false;
	settings.left.clampEnabled = true;

	qss::RecalculateRecordingOutputs(recording, settings);

	ASSERT_EQ(recording.samples.size(), 2U);
	EXPECT_FLOAT_EQ(recording.samples[0].outputValue, 0.4F);
	EXPECT_FLOAT_EQ(recording.samples[1].outputValue, 0.3F);
	EXPECT_NE(recording.samples[0].flags & qss::kSampleFlagCorrectionApplied, 0U);
	EXPECT_NE(recording.samples[1].flags & qss::kSampleFlagCorrectionApplied, 0U);
}

TEST(ReplayTests, UsesInitialOtherAxisWhenFirstRecordedSampleIsOneAxis) {
	qss::RecordingData recording;
	recording.initialLeftY = 0.8F;
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Left,
		qss::ScalarSemantic::JoystickX
	));
	recording.samples.push_back({100, 1, 0, 0.8F, 0.8F, 0.0, 0});

	qss::ReplayCorrectionSettings settings;
	settings.left.enabled = true;
	settings.left.centerOffsetEnabled = false;
	settings.left.innerDeadzoneEnabled = false;
	settings.left.outerNormalizationEnabled = false;
	settings.left.clampEnabled = true;

	qss::RecalculateRecordingOutputs(recording, settings);

	EXPECT_NEAR(recording.samples[0].outputValue, 0.7071F, 0.001F);
}

TEST(ReplayTests, UsesRadialDeadzoneAcrossAxes) {
	qss::RecordingData recording;
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Left,
		qss::ScalarSemantic::JoystickX
	));
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Left,
		qss::ScalarSemantic::JoystickY
	));

	recording.samples.push_back({100, 1, 0, 0.04F, 0.04F, 0.0, 0});
	recording.samples.push_back({101, 2, 1, 0.03F, 0.03F, 0.0, 0});

	qss::ReplayCorrectionSettings settings;
	settings.left.enabled = true;
	settings.left.centerOffsetEnabled = false;
	settings.left.innerDeadzone = 0.10F;
	settings.left.outerNormalizationEnabled = false;

	qss::RecalculateRecordingOutputs(recording, settings);

	EXPECT_FLOAT_EQ(recording.samples[0].outputValue, 0.0F);
	EXPECT_FLOAT_EQ(recording.samples[1].outputValue, 0.0F);
}

TEST(ReplayTests, ReplaysSmoothingUsingRecordedTimestamps) {
	qss::RecordingData recording;
	recording.qpcFrequency = 1000;
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Left,
		qss::ScalarSemantic::JoystickX
	));
	recording.samples.push_back({1000, 1, 0, 1.0F, 1.0F, 0.0, 0});
	recording.samples.push_back({1010, 2, 0, 0.0F, 0.0F, 0.0, 0});

	qss::ReplayCorrectionSettings settings;
	settings.left.enabled = true;
	settings.left.centerOffsetEnabled = false;
	settings.left.innerDeadzoneEnabled = false;
	settings.left.outerNormalizationEnabled = false;
	settings.left.smoothing = 1.0F;

	qss::RecalculateRecordingOutputs(recording, settings);

	EXPECT_FLOAT_EQ(recording.samples[0].outputValue, 1.0F);
	EXPECT_GT(recording.samples[1].outputValue, 0.0F);
	EXPECT_LT(recording.samples[1].outputValue, 1.0F);
}

TEST(ReplayTests, LeavesUnknownComponentsPassThrough) {
	qss::RecordingData recording;
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Unknown,
		qss::ScalarSemantic::Unknown
	));
	recording.samples.push_back({
		100,
		1,
		0,
		0.75F,
		-0.25F,
		0.0,
		qss::kSampleFlagCorrectionApplied
	});

	qss::ReplayCorrectionSettings settings;
	settings.left.enabled = true;
	settings.right.enabled = true;

	qss::RecalculateRecordingOutputs(recording, settings);

	EXPECT_FLOAT_EQ(recording.samples[0].outputValue, 0.75F);
	EXPECT_EQ(recording.samples[0].flags & qss::kSampleFlagCorrectionApplied, 0U);
}

TEST(ReplayTests, DisabledCorrectionRestoresRawOutput) {
	qss::RecordingData recording;
	recording.components.push_back(MakeComponent(
		qss::ControllerHand::Right,
		qss::ScalarSemantic::JoystickX
	));
	recording.samples.push_back({
		100,
		1,
		0,
		0.5F,
		0.1F,
		0.0,
		qss::kSampleFlagCorrectionApplied
	});

	qss::ReplayCorrectionSettings settings;
	settings.right.enabled = false;

	qss::RecalculateRecordingOutputs(recording, settings);

	EXPECT_FLOAT_EQ(recording.samples[0].outputValue, 0.5F);
	EXPECT_EQ(recording.samples[0].flags & qss::kSampleFlagCorrectionApplied, 0U);
}

} // namespace
