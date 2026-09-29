#include "core/analysis/WindowsInputAnalysis.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

TEST(WindowsInputAnalysisTests, SeparatesLowLevelAndRawInput) {
	std::vector<qss::WindowsInputSample> samples;

	qss::WindowsInputSample lowMove;
	lowMove.timestampTicks = 0;
	lowMove.source = qss::WindowsInputSource::LowLevelMouse;
	lowMove.kind = qss::WindowsInputKind::Move;
	lowMove.flags = 1;
	samples.push_back(lowMove);

	qss::WindowsInputSample lowWheel;
	lowWheel.timestampTicks = 100;
	lowWheel.source = qss::WindowsInputSource::LowLevelMouse;
	lowWheel.kind = qss::WindowsInputKind::Wheel;
	lowWheel.wheelDelta = -120;
	samples.push_back(lowWheel);

	qss::WindowsInputSample rawMove;
	rawMove.timestampTicks = 200;
	rawMove.source = qss::WindowsInputSource::RawInputMouse;
	rawMove.kind = qss::WindowsInputKind::Move;
	samples.push_back(rawMove);

	const qss::WindowsInputStatistics result =
		qss::AnalyzeWindowsInput(samples, 100);

	EXPECT_EQ(result.lowLevelMouse.total, 2U);
	EXPECT_EQ(result.lowLevelMouse.move, 1U);
	EXPECT_EQ(result.lowLevelMouse.wheel, 1U);
	EXPECT_EQ(result.lowLevelMouse.wheelDeltaSum, -120);
	EXPECT_EQ(result.lowLevelInjected, 1U);
	EXPECT_EQ(result.rawInputMouse.total, 1U);
	EXPECT_EQ(result.rawInputMouse.move, 1U);
	EXPECT_DOUBLE_EQ(result.durationSeconds, 2.0);
}

TEST(WindowsInputAnalysisTests, EmptyInputProducesZeroStatistics) {
	const qss::WindowsInputStatistics result =
		qss::AnalyzeWindowsInput({}, 10000000);

	EXPECT_EQ(result.lowLevelMouse.total, 0U);
	EXPECT_EQ(result.rawInputMouse.total, 0U);
	EXPECT_EQ(result.lowLevelInjected, 0U);
	EXPECT_DOUBLE_EQ(result.durationSeconds, 0.0);
}

} // namespace
