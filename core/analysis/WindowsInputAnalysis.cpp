#include "core/analysis/WindowsInputAnalysis.hpp"

namespace qss {
namespace {

void AddSample(
	WindowsInputSourceStatistics& statistics,
	const WindowsInputSample& sample
) {
	++statistics.total;
	switch (sample.kind) {
	case WindowsInputKind::Move:
		++statistics.move;
		break;
	case WindowsInputKind::Wheel:
		++statistics.wheel;
		statistics.wheelDeltaSum += sample.wheelDelta;
		break;
	case WindowsInputKind::HorizontalWheel:
		++statistics.horizontalWheel;
		statistics.wheelDeltaSum += sample.wheelDelta;
		break;
	case WindowsInputKind::Button:
		++statistics.button;
		break;
	case WindowsInputKind::Unknown:
		break;
	}
}

} // namespace

WindowsInputStatistics AnalyzeWindowsInput(
	const std::vector<WindowsInputSample>& samples,
	std::int64_t qpcFrequency
) {
	WindowsInputStatistics result;
	if (samples.empty()) {
		return result;
	}

	for (const WindowsInputSample& sample : samples) {
		if (sample.source == WindowsInputSource::LowLevelMouse) {
			AddSample(result.lowLevelMouse, sample);
			if ((sample.flags & 0x00000001U) != 0U) {
				++result.lowLevelInjected;
			}
		} else if (sample.source == WindowsInputSource::RawInputMouse) {
			AddSample(result.rawInputMouse, sample);
		}
	}

	if (qpcFrequency > 0) {
		const std::int64_t elapsed =
			samples.back().timestampTicks - samples.front().timestampTicks;
		if (elapsed > 0) {
			result.durationSeconds =
				static_cast<double>(elapsed) /
				static_cast<double>(qpcFrequency);
		}
	}
	return result;
}

} // namespace qss
