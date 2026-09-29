#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace qss {

inline constexpr std::uint32_t kSteamVRSharedMagic = 0x51535331U;
inline constexpr std::uint32_t kSteamVRSharedVersion = 4;
inline constexpr std::size_t kSteamVRMaxScalarComponents = 256;
inline constexpr std::size_t kSteamVRSampleCapacity = 8192;
inline constexpr std::size_t kSteamVRComponentPathCapacity = 96;
inline constexpr std::size_t kSharedOuterDirectionCount = 64;
inline constexpr std::uint32_t kSampleFlagCorrectionApplied = 1U << 0;
inline constexpr std::uint32_t kSampleFlagUnknownComponent = 1U << 1;

inline constexpr wchar_t kSteamVRSharedMemoryName[] = L"Local\\QuestStickScope.SteamVR.v4";

enum class ProbeState : std::uint32_t {
	Offline = 0,
	PassThrough = 1,
	Observing = 2,
	Error = 3,
};

enum class ControllerHand : std::uint32_t {
	Unknown = 0,
	Left = 1,
	Right = 2,
};

enum class ScalarSemantic : std::uint32_t {
	Unknown = 0,
	JoystickX = 1,
	JoystickY = 2,
};

struct SharedScalarComponent {
	std::atomic<std::uint64_t> stamp{0};
	std::uint64_t handle = 0;
	std::uint64_t container = 0;
	std::int32_t scalarType = 0;
	std::int32_t scalarUnits = 0;
	ControllerHand hand = ControllerHand::Unknown;
	ScalarSemantic semantic = ScalarSemantic::Unknown;
	std::array<char, kSteamVRComponentPathCapacity> path{};
};

struct ScalarComponentSnapshot {
	std::uint64_t handle = 0;
	std::uint64_t container = 0;
	std::int32_t scalarType = 0;
	std::int32_t scalarUnits = 0;
	ControllerHand hand = ControllerHand::Unknown;
	ScalarSemantic semantic = ScalarSemantic::Unknown;
	std::array<char, kSteamVRComponentPathCapacity> path{};
};

struct SharedHandCorrection {
	std::uint32_t enabled = 0;
	std::uint32_t centerOffsetEnabled = 1;
	std::uint32_t innerDeadzoneEnabled = 1;
	std::uint32_t outerNormalizationEnabled = 1;
	std::uint32_t clampEnabled = 1;
	float centerX = 0.0F;
	float centerY = 0.0F;
	float innerDeadzone = 0.0F;
	std::array<float, kSharedOuterDirectionCount> outerRadius{};
};

struct CorrectionControlSnapshot {
	SharedHandCorrection left{};
	SharedHandCorrection right{};
	std::uint64_t revision = 0;
};

struct SharedCorrectionControl {
	std::atomic<std::uint64_t> stamp{0};
	SharedHandCorrection left{};
	SharedHandCorrection right{};
};

struct SharedScalarSample {
	std::int64_t timestampTicks = 0;
	std::uint64_t sequence = 0;
	std::uint32_t componentIndex = 0;
	float rawValue = 0.0F;
	float outputValue = 0.0F;
	double timeOffset = 0.0;
	std::uint32_t flags = 0;
};

struct SharedScalarSampleSlot {
	std::atomic<std::uint64_t> stamp{0};
	SharedScalarSample sample{};
};

struct SteamVRSharedState {
	std::uint32_t magic = kSteamVRSharedMagic;
	std::uint32_t version = kSteamVRSharedVersion;
	std::uint64_t qpcFrequency = 0;
	std::atomic<std::uint64_t> sessionId{0};
	std::atomic<std::uint32_t> probeState{static_cast<std::uint32_t>(ProbeState::Offline)};
	std::atomic<std::uint64_t> heartbeatTicks{0};
	std::atomic<std::uint64_t> clientHeartbeatTicks{0};
	SharedCorrectionControl correction{};
	std::atomic<std::uint32_t> componentCount{0};
	std::array<SharedScalarComponent, kSteamVRMaxScalarComponents> components{};
	std::atomic<std::uint64_t> writeSequence{0};
	std::array<SharedScalarSampleSlot, kSteamVRSampleCapacity> samples{};
};

} // namespace qss
