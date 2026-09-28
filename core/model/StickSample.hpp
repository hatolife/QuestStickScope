#pragma once

#include "core/model/StickTypes.hpp"

#include <cstdint>

namespace qss {

struct StickSample {
	std::int64_t timestampTicks = 0;
	Layer layer = Layer::Q0Physical;
	Hand hand = Hand::Left;
	Axis axis = Axis::X;
	float rawValue = 0.0F;
	float outputValue = 0.0F;
	std::uint64_t sequence = 0;
	SampleState state = SampleState::Unavailable;
	std::uint32_t flags = 0;
};

struct StickPairSample {
	std::int64_t timestampTicks = 0;
	Layer layer = Layer::Q0Physical;
	Hand hand = Hand::Left;
	Vec2 raw{};
	Vec2 output{};
	std::uint64_t sequence = 0;
	SampleState state = SampleState::Unavailable;
	std::uint32_t flags = 0;
};

} // namespace qss
