#include "core/live/SteamVRLiveState.hpp"

namespace qss {

void SteamVRLiveState::Reset() noexcept {
	m_targets.fill(Target::None);
	m_left = {};
	m_right = {};
	m_lastSequence = 0;
	m_droppedSampleCount = 0;
}

void SteamVRLiveState::ConfigureComponent(
	std::uint32_t componentIndex,
	const ScalarComponentSnapshot& component
) noexcept {
	if (componentIndex >= m_targets.size()) {
		return;
	}
	m_targets[componentIndex] = ResolveTarget(component);
}

void SteamVRLiveState::ConsumeSample(const SharedScalarSample& sample) noexcept {
	if (sample.sequence != 0) {
		if (m_lastSequence != 0 && sample.sequence > m_lastSequence + 1) {
			m_droppedSampleCount += sample.sequence - m_lastSequence - 1;
		}
		if (sample.sequence > m_lastSequence) {
			m_lastSequence = sample.sequence;
		}
	}

	if (sample.componentIndex >= m_targets.size()) {
		return;
	}

	LiveAxisState* axis = ResolveAxis(m_targets[sample.componentIndex]);
	if (axis == nullptr) {
		return;
	}

	axis->rawValue = sample.rawValue;
	axis->outputValue = sample.outputValue;
	axis->timestampTicks = sample.timestampTicks;
	axis->sequence = sample.sequence;
	axis->available = true;
}

const LiveStickState& SteamVRLiveState::GetLeft() const noexcept {
	return m_left;
}

const LiveStickState& SteamVRLiveState::GetRight() const noexcept {
	return m_right;
}

std::uint64_t SteamVRLiveState::GetLastSequence() const noexcept {
	return m_lastSequence;
}

std::uint64_t SteamVRLiveState::GetDroppedSampleCount() const noexcept {
	return m_droppedSampleCount;
}

SteamVRLiveState::Target SteamVRLiveState::ResolveTarget(
	const ScalarComponentSnapshot& component
) noexcept {
	if (component.hand == ControllerHand::Left) {
		if (component.semantic == ScalarSemantic::JoystickX) {
			return Target::LeftX;
		}
		if (component.semantic == ScalarSemantic::JoystickY) {
			return Target::LeftY;
		}
	}
	if (component.hand == ControllerHand::Right) {
		if (component.semantic == ScalarSemantic::JoystickX) {
			return Target::RightX;
		}
		if (component.semantic == ScalarSemantic::JoystickY) {
			return Target::RightY;
		}
	}
	return Target::None;
}

LiveAxisState* SteamVRLiveState::ResolveAxis(Target target) noexcept {
	switch (target) {
	case Target::LeftX:
		return &m_left.x;
	case Target::LeftY:
		return &m_left.y;
	case Target::RightX:
		return &m_right.x;
	case Target::RightY:
		return &m_right.y;
	case Target::None:
		return nullptr;
	}
	return nullptr;
}

} // namespace qss
