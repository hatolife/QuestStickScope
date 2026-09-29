#include "platform/windows/WindowsXInputObserver.hpp"

#ifdef _WIN32

#include <Windows.h>
#include <Xinput.h>

namespace qss {
namespace {

float NormalizeThumb(std::int16_t value) noexcept {
	if (value < 0) {
		return static_cast<float>(value) / 32768.0F;
	}
	return static_cast<float>(value) / 32767.0F;
}

} // namespace

void WindowsXInputObserver::Poll() noexcept {
	for (DWORD index = 0; index < static_cast<DWORD>(m_slots.size()); ++index) {
		XINPUT_STATE state{};
		const DWORD result = ::XInputGetState(index, &state);
		XInputSlotState& output = m_slots[index];
		if (result != ERROR_SUCCESS) {
			output = {};
			continue;
		}

		output.connected = true;
		output.packetNumber = state.dwPacketNumber;
		output.buttons = state.Gamepad.wButtons;
		output.leftTrigger = state.Gamepad.bLeftTrigger;
		output.rightTrigger = state.Gamepad.bRightTrigger;
		output.leftXRaw = state.Gamepad.sThumbLX;
		output.leftYRaw = state.Gamepad.sThumbLY;
		output.rightXRaw = state.Gamepad.sThumbRX;
		output.rightYRaw = state.Gamepad.sThumbRY;
		output.leftX = NormalizeThumb(output.leftXRaw);
		output.leftY = NormalizeThumb(output.leftYRaw);
		output.rightX = NormalizeThumb(output.rightXRaw);
		output.rightY = NormalizeThumb(output.rightYRaw);
	}
}

const std::array<XInputSlotState, 4>& WindowsXInputObserver::GetSlots() const noexcept {
	return m_slots;
}

} // namespace qss

#endif
