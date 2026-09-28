#pragma once

#include "core/model/StickTypes.hpp"
#include "ipc/SharedProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace qss {

struct LiveAxisState {
	float rawValue = 0.0F;
	float outputValue = 0.0F;
	std::int64_t timestampTicks = 0;
	std::uint64_t sequence = 0;
	bool available = false;
};

struct LiveStickState {
	LiveAxisState x{};
	LiveAxisState y{};
};

class SteamVRLiveState {
public:
	void Reset() noexcept;
	void ConfigureComponent(std::uint32_t componentIndex, const ScalarComponentSnapshot& component) noexcept;
	void ConsumeSample(const SharedScalarSample& sample) noexcept;

	const LiveStickState& GetLeft() const noexcept;
	const LiveStickState& GetRight() const noexcept;
	std::uint64_t GetLastSequence() const noexcept;
	std::uint64_t GetDroppedSampleCount() const noexcept;

private:
	enum class Target : std::uint8_t {
		None,
		LeftX,
		LeftY,
		RightX,
		RightY,
	};

	static Target ResolveTarget(const ScalarComponentSnapshot& component) noexcept;
	LiveAxisState* ResolveAxis(Target target) noexcept;

	std::array<Target, kSteamVRMaxScalarComponents> m_targets{};
	LiveStickState m_left{};
	LiveStickState m_right{};
	std::uint64_t m_lastSequence = 0;
	std::uint64_t m_droppedSampleCount = 0;
};

} // namespace qss
