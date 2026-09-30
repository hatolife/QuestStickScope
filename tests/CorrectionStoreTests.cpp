#include "core/config/CorrectionStore.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path TempPath(const char* name) {
	return std::filesystem::temp_directory_path() / name;
}

TEST(CorrectionStoreTests, RoundTripsCorrectionSettings) {
	const std::filesystem::path path = TempPath("QuestStickScope-calibration-roundtrip.json");
	qss::SharedHandCorrection left = qss::MakeDefaultSharedCorrection();
	qss::SharedHandCorrection right = qss::MakeDefaultSharedCorrection();
	left.enabled = 1;
	left.centerX = 0.08F;
	left.centerY = -0.03F;
	left.innerDeadzone = 0.12F;
	left.innerRadius[17] = 0.07F;
	left.outerRadius[17] = 0.84F;

	std::string error;
	ASSERT_TRUE(qss::SaveCorrectionSettings(path, left, right, &error)) << error;

	qss::SharedHandCorrection loadedLeft;
	qss::SharedHandCorrection loadedRight;
	ASSERT_TRUE(qss::LoadCorrectionSettings(path, loadedLeft, loadedRight, &error)) << error;
	EXPECT_EQ(loadedLeft.enabled, 1U);
	EXPECT_FLOAT_EQ(loadedLeft.centerX, 0.08F);
	EXPECT_FLOAT_EQ(loadedLeft.centerY, -0.03F);
	EXPECT_FLOAT_EQ(loadedLeft.innerDeadzone, 0.12F);
	EXPECT_FLOAT_EQ(loadedLeft.innerRadius[17], 0.07F);
	EXPECT_FLOAT_EQ(loadedLeft.outerRadius[17], 0.84F);
	EXPECT_FLOAT_EQ(loadedRight.outerRadius[17], 1.0F);

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(CorrectionStoreTests, InvalidFileFallsBackToSafeDisabledDefaults) {
	const std::filesystem::path path = TempPath("QuestStickScope-calibration-invalid.json");
	{
		std::ofstream stream(path, std::ios::trunc);
		stream << R"({"version":1,"left":{"enabled":true},"right":{}})";
	}

	qss::SharedHandCorrection left;
	qss::SharedHandCorrection right;
	std::string error;
	EXPECT_FALSE(qss::LoadCorrectionSettings(path, left, right, &error));
	EXPECT_EQ(left.enabled, 0U);
	EXPECT_EQ(right.enabled, 0U);
	for (const float radius : left.innerRadius) {
		EXPECT_FLOAT_EQ(radius, 0.0F);
	}
	for (const float radius : left.outerRadius) {
		EXPECT_FLOAT_EQ(radius, 1.0F);
	}
	for (const float radius : right.innerRadius) {
		EXPECT_FLOAT_EQ(radius, 0.0F);
	}
	for (const float radius : right.outerRadius) {
		EXPECT_FLOAT_EQ(radius, 1.0F);
	}

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST(CorrectionStoreTests, RejectsUnsafeOuterRadius) {
	const std::filesystem::path path = TempPath("QuestStickScope-calibration-unsafe.json");
	qss::SharedHandCorrection left = qss::MakeDefaultSharedCorrection();
	qss::SharedHandCorrection right = qss::MakeDefaultSharedCorrection();
	left.outerRadius[3] = 0.0F;

	std::string error;
	ASSERT_TRUE(qss::SaveCorrectionSettings(path, left, right, &error));

	qss::SharedHandCorrection loadedLeft;
	qss::SharedHandCorrection loadedRight;
	EXPECT_FALSE(qss::LoadCorrectionSettings(path, loadedLeft, loadedRight, &error));
	EXPECT_EQ(loadedLeft.enabled, 0U);
	EXPECT_FLOAT_EQ(loadedLeft.outerRadius[3], 1.0F);

	std::error_code ec;
	std::filesystem::remove(path, ec);
}

} // namespace
