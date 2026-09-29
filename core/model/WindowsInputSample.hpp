#pragma once

#include <cstdint>

namespace qss {

enum class WindowsInputSource : std::uint8_t {
	LowLevelMouse,
	RawInputMouse,
};

enum class WindowsInputKind : std::uint8_t {
	Move,
	Wheel,
	HorizontalWheel,
	Button,
	Unknown,
};

struct WindowsInputSample {
	std::int64_t timestampTicks = 0;
	WindowsInputSource source = WindowsInputSource::LowLevelMouse;
	WindowsInputKind kind = WindowsInputKind::Unknown;
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::int32_t deltaX = 0;
	std::int32_t deltaY = 0;
	std::int32_t wheelDelta = 0;
	std::uint32_t flags = 0;
	std::uint64_t device = 0;
	std::uint64_t extraInfo = 0;
};

} // namespace qss
