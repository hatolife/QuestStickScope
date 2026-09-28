#include "core/correction/Correction.hpp"
#include "platform/Clock.hpp"

#include <iostream>

namespace {

int RunApplication() {
	qss::CorrectionSettings settings;
	settings.enabled = false;

	const qss::CorrectionResult result = qss::ApplyCorrection({0.0F, 0.0F}, settings);

	std::cout << "QuestStickScope development build\n";
	std::cout << "clock_frequency=" << qss::MonotonicClock::Frequency() << '\n';
	std::cout << "correction_output=(" << result.output.x << ", " << result.output.y << ")\n";
	return 0;
}

} // namespace

int main() {
	return RunApplication();
}
