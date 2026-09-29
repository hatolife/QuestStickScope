#pragma once

#include "core/model/WindowsInputSample.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qss {

struct WindowsInputSourceStatistics {
	std::size_t total = 0;
	std::size_t move = 0;
	std::size_t wheel = 0;
	std::size_t horizontalWheel = 0;
	std::size_t button = 0;
	std::int64_t wheelDeltaSum = 0;
};

struct WindowsInputStatistics {
	WindowsInputSourceStatistics lowLevelMouse{};
	WindowsInputSourceStatistics rawInputMouse{};
	std::size_t lowLevelInjected = 0;
	double durationSeconds = 0.0;
};

WindowsInputStatistics AnalyzeWindowsInput(
	const std::vector<WindowsInputSample>& samples,
	std::int64_t qpcFrequency
);

} // namespace qss
