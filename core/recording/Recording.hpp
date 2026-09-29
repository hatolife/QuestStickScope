#pragma once

#include "core/model/WindowsInputSample.hpp"
#include "ipc/SharedProtocol.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace qss {

struct RecordingData {
	std::uint64_t qpcFrequency = 0;
	std::uint64_t sessionId = 0;
	ProbeState probeState = ProbeState::Offline;
	SharedHandCorrection leftCorrection{};
	SharedHandCorrection rightCorrection{};
	float initialLeftX = 0.0F;
	float initialLeftY = 0.0F;
	float initialRightX = 0.0F;
	float initialRightY = 0.0F;
	std::vector<ScalarComponentSnapshot> components;
	std::vector<SharedScalarSample> samples;
	std::vector<WindowsInputSample> windowsInputSamples;
};

bool SaveRecording(
	const std::filesystem::path& path,
	const RecordingData& recording,
	std::string* errorMessage = nullptr
);

bool LoadRecording(
	const std::filesystem::path& path,
	RecordingData& recording,
	std::string* errorMessage = nullptr
);

} // namespace qss
