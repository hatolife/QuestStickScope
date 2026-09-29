#include "core/recording/Recording.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <utility>

namespace qss {
namespace {

constexpr std::array<char, 8> kMagic = {'Q', 'S', 'S', 'R', 'E', 'C', '1', '\0'};
constexpr std::uint32_t kFormatVersion = 2;
constexpr std::uint32_t kMaxComponentCount = 4096;
constexpr std::uint64_t kMaxSampleCount = 100000000ULL;

template <typename T>
bool WriteValue(std::ostream& stream, const T& value) {
	static_assert(std::is_trivially_copyable_v<T>);
	stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
	return stream.good();
}

template <typename T>
bool ReadValue(std::istream& stream, T& value) {
	static_assert(std::is_trivially_copyable_v<T>);
	stream.read(reinterpret_cast<char*>(&value), sizeof(T));
	return stream.good();
}

bool WriteBytes(std::ostream& stream, const char* data, std::size_t size) {
	stream.write(data, static_cast<std::streamsize>(size));
	return stream.good();
}

bool ReadBytes(std::istream& stream, char* data, std::size_t size) {
	stream.read(data, static_cast<std::streamsize>(size));
	return stream.good();
}

void SetError(std::string* errorMessage, const char* message) {
	if (errorMessage != nullptr) {
		*errorMessage = message;
	}
}

bool WriteComponent(std::ostream& stream, const ScalarComponentSnapshot& component) {
	return WriteValue(stream, component.handle) &&
		WriteValue(stream, component.container) &&
		WriteValue(stream, component.scalarType) &&
		WriteValue(stream, component.scalarUnits) &&
		WriteValue(stream, static_cast<std::uint32_t>(component.hand)) &&
		WriteValue(stream, static_cast<std::uint32_t>(component.semantic)) &&
		WriteBytes(stream, component.path.data(), component.path.size());
}

bool ReadComponent(std::istream& stream, ScalarComponentSnapshot& component) {
	std::uint32_t hand = 0;
	std::uint32_t semantic = 0;
	if (!ReadValue(stream, component.handle) ||
		!ReadValue(stream, component.container) ||
		!ReadValue(stream, component.scalarType) ||
		!ReadValue(stream, component.scalarUnits) ||
		!ReadValue(stream, hand) ||
		!ReadValue(stream, semantic) ||
		!ReadBytes(stream, component.path.data(), component.path.size())) {
		return false;
	}

	component.path.back() = '\0';
	if (hand > static_cast<std::uint32_t>(ControllerHand::Right)) {
		hand = static_cast<std::uint32_t>(ControllerHand::Unknown);
	}
	if (semantic > static_cast<std::uint32_t>(ScalarSemantic::JoystickY)) {
		semantic = static_cast<std::uint32_t>(ScalarSemantic::Unknown);
	}
	component.hand = static_cast<ControllerHand>(hand);
	component.semantic = static_cast<ScalarSemantic>(semantic);
	return true;
}

bool WriteHandCorrection(std::ostream& stream, const SharedHandCorrection& correction) {
	if (!WriteValue(stream, correction.enabled) ||
		!WriteValue(stream, correction.centerOffsetEnabled) ||
		!WriteValue(stream, correction.innerDeadzoneEnabled) ||
		!WriteValue(stream, correction.outerNormalizationEnabled) ||
		!WriteValue(stream, correction.clampEnabled) ||
		!WriteValue(stream, correction.centerX) ||
		!WriteValue(stream, correction.centerY) ||
		!WriteValue(stream, correction.innerDeadzone)) {
		return false;
	}
	for (const float radius : correction.outerRadius) {
		if (!WriteValue(stream, radius)) {
			return false;
		}
	}
	return true;
}

bool ReadHandCorrection(std::istream& stream, SharedHandCorrection& correction) {
	if (!ReadValue(stream, correction.enabled) ||
		!ReadValue(stream, correction.centerOffsetEnabled) ||
		!ReadValue(stream, correction.innerDeadzoneEnabled) ||
		!ReadValue(stream, correction.outerNormalizationEnabled) ||
		!ReadValue(stream, correction.clampEnabled) ||
		!ReadValue(stream, correction.centerX) ||
		!ReadValue(stream, correction.centerY) ||
		!ReadValue(stream, correction.innerDeadzone)) {
		return false;
	}
	for (float& radius : correction.outerRadius) {
		if (!ReadValue(stream, radius)) {
			return false;
		}
	}
	return true;
}

bool WriteSample(std::ostream& stream, const SharedScalarSample& sample) {
	return WriteValue(stream, sample.timestampTicks) &&
		WriteValue(stream, sample.sequence) &&
		WriteValue(stream, sample.componentIndex) &&
		WriteValue(stream, sample.rawValue) &&
		WriteValue(stream, sample.outputValue) &&
		WriteValue(stream, sample.timeOffset) &&
		WriteValue(stream, sample.flags);
}

bool ReadSample(std::istream& stream, SharedScalarSample& sample) {
	return ReadValue(stream, sample.timestampTicks) &&
		ReadValue(stream, sample.sequence) &&
		ReadValue(stream, sample.componentIndex) &&
		ReadValue(stream, sample.rawValue) &&
		ReadValue(stream, sample.outputValue) &&
		ReadValue(stream, sample.timeOffset) &&
		ReadValue(stream, sample.flags);
}

} // namespace

bool SaveRecording(
	const std::filesystem::path& path,
	const RecordingData& recording,
	std::string* errorMessage
) {
	if (recording.components.size() > std::numeric_limits<std::uint32_t>::max()) {
		SetError(errorMessage, "Too many components.");
		return false;
	}

	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream) {
		SetError(errorMessage, "Failed to open recording file for writing.");
		return false;
	}

	const std::uint32_t componentCount = static_cast<std::uint32_t>(recording.components.size());
	const std::uint64_t sampleCount = static_cast<std::uint64_t>(recording.samples.size());
	if (!WriteBytes(stream, kMagic.data(), kMagic.size()) ||
		!WriteValue(stream, kFormatVersion) ||
		!WriteValue(stream, componentCount) ||
		!WriteValue(stream, sampleCount) ||
		!WriteValue(stream, recording.qpcFrequency) ||
		!WriteValue(stream, recording.sessionId) ||
		!WriteValue(stream, static_cast<std::uint32_t>(recording.probeState)) ||
		!WriteHandCorrection(stream, recording.leftCorrection) ||
		!WriteHandCorrection(stream, recording.rightCorrection)) {
		SetError(errorMessage, "Failed to write recording header.");
		return false;
	}

	for (const ScalarComponentSnapshot& component : recording.components) {
		if (!WriteComponent(stream, component)) {
			SetError(errorMessage, "Failed to write component table.");
			return false;
		}
	}
	for (const SharedScalarSample& sample : recording.samples) {
		if (!WriteSample(stream, sample)) {
			SetError(errorMessage, "Failed to write recording samples.");
			return false;
		}
	}

	if (!stream.flush()) {
		SetError(errorMessage, "Failed to flush recording file.");
		return false;
	}
	return true;
}

bool LoadRecording(
	const std::filesystem::path& path,
	RecordingData& recording,
	std::string* errorMessage
) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		SetError(errorMessage, "Failed to open recording file.");
		return false;
	}

	std::array<char, kMagic.size()> magic{};
	std::uint32_t version = 0;
	std::uint32_t componentCount = 0;
	std::uint64_t sampleCount = 0;
	std::uint32_t probeState = 0;
	RecordingData loaded;

	if (!ReadBytes(stream, magic.data(), magic.size()) ||
		!ReadValue(stream, version) ||
		!ReadValue(stream, componentCount) ||
		!ReadValue(stream, sampleCount) ||
		!ReadValue(stream, loaded.qpcFrequency) ||
		!ReadValue(stream, loaded.sessionId) ||
		!ReadValue(stream, probeState) ||
		!ReadHandCorrection(stream, loaded.leftCorrection) ||
		!ReadHandCorrection(stream, loaded.rightCorrection)) {
		SetError(errorMessage, "Recording header is truncated.");
		return false;
	}
	if (magic != kMagic) {
		SetError(errorMessage, "Recording magic does not match.");
		return false;
	}
	if (version != kFormatVersion) {
		SetError(errorMessage, "Recording version is not supported.");
		return false;
	}
	if (probeState > static_cast<std::uint32_t>(ProbeState::Error)) {
		SetError(errorMessage, "Recording contains an invalid Probe state.");
		return false;
	}
	loaded.probeState = static_cast<ProbeState>(probeState);
	if (componentCount > kMaxComponentCount || sampleCount > kMaxSampleCount) {
		SetError(errorMessage, "Recording counts exceed safety limits.");
		return false;
	}

	loaded.components.resize(componentCount);
	for (ScalarComponentSnapshot& component : loaded.components) {
		if (!ReadComponent(stream, component)) {
			SetError(errorMessage, "Component table is truncated.");
			return false;
		}
	}

	loaded.samples.resize(static_cast<std::size_t>(sampleCount));
	for (SharedScalarSample& sample : loaded.samples) {
		if (!ReadSample(stream, sample)) {
			SetError(errorMessage, "Recording sample data is truncated.");
			return false;
		}
		if (sample.componentIndex >= componentCount) {
			SetError(errorMessage, "Recording contains an invalid component index.");
			return false;
		}
	}

	recording = std::move(loaded);
	return true;
}

} // namespace qss
