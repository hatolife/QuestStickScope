#include "core/correction/Correction.hpp"
#include "platform/Clock.hpp"

#ifdef QSS_WINDOWS_IPC
#include "platform/windows/SteamVRSharedMemory.hpp"
#endif

#include <cstring>
#include <iostream>

namespace {

#ifdef QSS_WINDOWS_IPC
const char* ProbeStateName(qss::ProbeState state) {
	switch (state) {
	case qss::ProbeState::Offline:
		return "Offline";
	case qss::ProbeState::PassThrough:
		return "Pass-through";
	case qss::ProbeState::Observing:
		return "Observing";
	case qss::ProbeState::Error:
		return "Error";
	}
	return "Unknown";
}

int PrintSteamVRStatus() {
	qss::SteamVRSharedMemoryReader reader;
	if (!reader.Open()) {
		std::cout << "SteamVR Probe: Offline\n";
		return 1;
	}

	std::cout << "SteamVR Probe: " << ProbeStateName(reader.GetProbeState()) << '\n';
	std::cout << "Scalar components: " << reader.GetComponentCount() << '\n';

	for (std::uint32_t index = 0; index < reader.GetComponentCount(); ++index) {
		qss::SharedScalarComponent component;
		if (!reader.ReadComponent(index, component)) {
			continue;
		}
		std::cout << "[" << index << "] " << component.path.data()
			<< " handle=" << component.handle
			<< " container=" << component.container << '\n';
	}

	qss::SharedScalarSample sample;
	if (reader.ReadLatestSample(sample)) {
		std::cout << "Latest scalar: component=" << sample.componentIndex
			<< " raw=" << sample.rawValue
			<< " output=" << sample.outputValue
			<< " sequence=" << sample.sequence << '\n';
	}
	return 0;
}
#endif

int RunApplication(int argc, char** argv) {
#ifdef QSS_WINDOWS_IPC
	if (argc >= 2 && std::strcmp(argv[1], "--steamvr-status") == 0) {
		return PrintSteamVRStatus();
	}
#else
	(void)argc;
	(void)argv;
#endif

	qss::CorrectionSettings settings;
	settings.enabled = false;
	const qss::CorrectionResult result = qss::ApplyCorrection({0.0F, 0.0F}, settings);

	std::cout << "QuestStickScope development build\n";
	std::cout << "clock_frequency=" << qss::MonotonicClock::Frequency() << '\n';
	std::cout << "correction_output=(" << result.output.x << ", " << result.output.y << ")\n";
	return 0;
}

} // namespace

int main(int argc, char** argv) {
	return RunApplication(argc, argv);
}
