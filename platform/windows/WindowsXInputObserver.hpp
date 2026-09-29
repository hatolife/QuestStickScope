#pragma once

#ifdef _WIN32

#include <array>
#include <cstdint>

namespace qss {

struct XInputSlotState {
	bool connected = false;
	std::uint32_t packetNumber = 0;
	std::uint16_t buttons = 0;
	std::uint8_t leftTrigger = 0;
	std::uint8_t rightTrigger = 0;
	std::int16_t leftXRaw = 0;
	std::int16_t leftYRaw = 0;
	std::int16_t rightXRaw = 0;
	std::int16_t rightYRaw = 0;
	float leftX = 0.0F;
	float leftY = 0.0F;
	float rightX = 0.0F;
	float rightY = 0.0F;
};

class WindowsXInputObserver {
public:
	void Poll() noexcept;
	const std::array<XInputSlotState, 4>& GetSlots() const noexcept;

private:
	std::array<XInputSlotState, 4> m_slots{};
};

} // namespace qss

#endif
