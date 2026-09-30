#include "core/recording/Recording.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

std::filesystem::path MakeTempPath(const char* name) {
	return std::filesystem::temp_directory_path() / name;
}

TEST(RecordingTests, RoundTripsComponentsAndSamples) {
	const std::filesystem::path path = MakeTempPath("QuestStickScope-recording-roundtrip.qssrec");

	qss::RecordingData source;
	source.qpcFrequency = 10000000;
	source.sessionId = 42;
	source.probeState = qss::ProbeState::Observing;
	source.leftCorrection.enabled = 1;
	source.leftCorrection.centerX = 0.08F;
	source.leftCorrection.innerDeadzone = 0.12F;
	source.leftCorrection.innerRadius.fill(0.04F);
	source.leftCorrection.outerRadius.fill(0.9F);
	source.rightCorrection.outerRadius.fill(1.0F);
	source.initialLeftX = 0.2F;
	source.initialLeftY = -0.3F;
	source.initialRightX = 0.4F;
	source.initialRightY = -0.5F;

	qss::ScalarComponentSnapshot component;
	component.handle = 123;
	component.container = 456;
	component.scalarType = 7;
	component.scalarUnits = 8;
	component.hand = qss::ControllerHand::Left;
	component.semantic = qss::ScalarSemantic::JoystickX;
	const char componentPath[] = "/input/joystick/x";
	std::copy(std::begin(componentPath), std::end(componentPath), component.path.begin());
	source.components.push_back(component);

	qss::SharedScalarSample sample;
	sample.timestampTicks = 1000;
	sample.sequence = 11;
	sample.componentIndex = 0;
	sample.rawValue = 0.25F;
	sample.outputValue = 0.20F;
	sample.timeOffset = -0.001;
	sample.flags = qss::kSampleFlagCorrectionApplied;
	source.samples.push_back(sample);

	qss::WindowsInputSample windowsSample;
	windowsSample.timestampTicks = 2000;
	windowsSample.source = qss::WindowsInputSource::LowLevelMouse;
	windowsSample.kind = qss::WindowsInputKind::Wheel;
	windowsSample.x = 100;
	windowsSample.y = 200;
	windowsSample.wheelDelta = -120;
	windowsSample.flags = 1;
	windowsSample.extraInfo = 1234;
	source.windowsInputSamples.push_back(windowsSample);

	std::string error;
	ASSERT_TRUE(qss::SaveRecording(path, source, &error)) << error;

	qss::RecordingData loaded;
	ASSERT_TRUE(qss::LoadRecording(path, loaded, &error)) << error;
	ASSERT_EQ(loaded.components.size(), 1U);
	ASSERT_EQ(loaded.samples.size(), 1U);
	EXPECT_EQ(loaded.qpcFrequency, source.qpcFrequency);
	EXPECT_EQ(loaded.sessionId, source.sessionId);
	EXPECT_EQ(loaded.probeState, qss::ProbeState::Observing);
	EXPECT_EQ(loaded.leftCorrection.enabled, 1U);
	EXPECT_FLOAT_EQ(loaded.leftCorrection.centerX, 0.08F);
	EXPECT_FLOAT_EQ(loaded.leftCorrection.innerDeadzone, 0.12F);
	EXPECT_FLOAT_EQ(loaded.leftCorrection.innerRadius[17], 0.04F);
	EXPECT_FLOAT_EQ(loaded.leftCorrection.outerRadius[17], 0.9F);
	EXPECT_FLOAT_EQ(loaded.initialLeftX, 0.2F);
	EXPECT_FLOAT_EQ(loaded.initialLeftY, -0.3F);
	EXPECT_FLOAT_EQ(loaded.initialRightX, 0.4F);
	EXPECT_FLOAT_EQ(loaded.initialRightY, -0.5F);
	EXPECT_EQ(loaded.components[0].hand, qss::ControllerHand::Left);
	EXPECT_EQ(loaded.components[0].semantic, qss::ScalarSemantic::JoystickX);
	EXPECT_STREQ(loaded.components[0].path.data(), "/input/joystick/x");
	EXPECT_EQ(loaded.samples[0].sequence, 11U);
	EXPECT_FLOAT_EQ(loaded.samples[0].rawValue, 0.25F);
	EXPECT_FLOAT_EQ(loaded.samples[0].outputValue, 0.20F);
	EXPECT_EQ(loaded.samples[0].flags, qss::kSampleFlagCorrectionApplied);
	ASSERT_EQ(loaded.windowsInputSamples.size(), 1U);
	EXPECT_EQ(loaded.windowsInputSamples[0].source, qss::WindowsInputSource::LowLevelMouse);
	EXPECT_EQ(loaded.windowsInputSamples[0].kind, qss::WindowsInputKind::Wheel);
	EXPECT_EQ(loaded.windowsInputSamples[0].wheelDelta, -120);
	EXPECT_EQ(loaded.windowsInputSamples[0].extraInfo, 1234U);

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(RecordingTests, RejectsInvalidMagic) {
	const std::filesystem::path path = MakeTempPath("QuestStickScope-recording-invalid.qssrec");
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << "not a recording";
	}

	qss::RecordingData loaded;
	std::string error;
	EXPECT_FALSE(qss::LoadRecording(path, loaded, &error));
	EXPECT_FALSE(error.empty());

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(RecordingTests, RejectsTruncatedSampleData) {
	const std::filesystem::path path = MakeTempPath("QuestStickScope-recording-truncated.qssrec");

	qss::RecordingData source;
	source.qpcFrequency = 10000000;
	qss::ScalarComponentSnapshot component;
	source.components.push_back(component);
	qss::SharedScalarSample sample;
	source.samples.push_back(sample);

	std::string error;
	ASSERT_TRUE(qss::SaveRecording(path, source, &error)) << error;

	const auto size = std::filesystem::file_size(path);
	std::filesystem::resize_file(path, size - 3);

	qss::RecordingData loaded;
	EXPECT_FALSE(qss::LoadRecording(path, loaded, &error));

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

} // namespace
