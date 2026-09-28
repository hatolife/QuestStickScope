#pragma once

#include <cstdint>

namespace qss {

enum class Hand : std::uint8_t {
	Left,
	Right,
};

enum class Axis : std::uint8_t {
	X,
	Y,
};

enum class Layer : std::uint8_t {
	Q0Physical,
	Q1QuestRuntime,
	V0VirtualDesktopQuest,
	V1VirtualDesktopStreamer,
	W0WindowsInput,
	S0SteamVRDriver,
	S1SteamVRInput,
	A0VRChat,
};

enum class SampleState : std::uint8_t {
	Observed,
	Derived,
	Corrected,
	Unavailable,
	Dropped,
	UnknownComponent,
};

struct Vec2 {
	float x = 0.0F;
	float y = 0.0F;
};

} // namespace qss
