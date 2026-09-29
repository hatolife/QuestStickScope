#include "core/recording/Recording.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path MakeTempPath(const char* name) {
	return std::filesystem::temp_directory_path() / name;
}

TEST(RecordingTests, RoundTripsComponentsAndSamples) {
	const std::filesystem::path path = MakeTempPath("QuestStickScope-recording-roundtrip.qssrec");

	qss::RecordingData source;
	source.qpcFrequency = 10000000;
	source.sessionId = 42;

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

	std::string error;
	ASSERT_TRUE(qss::SaveRecording(path, source, &error)) << error;

	qss::RecordingData loaded;
	ASSERT_TRUE(qss::LoadRecording(path, loaded, &error)) << error;
	ASSERT_EQ(loaded.components.size(), 1U);
	ASSERT_EQ(loaded.samples.size(), 1U);
	EXPECT_EQ(loaded.qpcFrequency, source.qpcFrequency);
	EXPECT_EQ(loaded.sessionId, source.sessionId);
	EXPECT_EQ(loaded.components[0].hand, qss::ControllerHand::Left);
	EXPECT_EQ(loaded.components[0].semantic, qss::ScalarSemantic::JoystickX);
	EXPECT_STREQ(loaded.components[0].path.data(), "/input/joystick/x");
	EXPECT_EQ(loaded.samples[0].sequence, 11U);
	EXPECT_FLOAT_EQ(loaded.samples[0].rawValue, 0.25F);
	EXPECT_FLOAT_EQ(loaded.samples[0].outputValue, 0.20F);
	EXPECT_EQ(loaded.samples[0].flags, qss::kSampleFlagCorrectionApplied);

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
