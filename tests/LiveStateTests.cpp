#include "core/live/SteamVRLiveState.hpp"

#include <gtest/gtest.h>

namespace {

qss::ScalarComponentSnapshot MakeComponent(qss::ControllerHand hand, qss::ScalarSemantic semantic) {
	qss::ScalarComponentSnapshot component;
	component.hand = hand;
	component.semantic = semantic;
	return component;
}

TEST(LiveStateTests, AggregatesLeftAndRightAxes) {
	qss::SteamVRLiveState state;
	state.ConfigureComponent(0, MakeComponent(qss::ControllerHand::Left, qss::ScalarSemantic::JoystickX));
	state.ConfigureComponent(1, MakeComponent(qss::ControllerHand::Left, qss::ScalarSemantic::JoystickY));
	state.ConfigureComponent(2, MakeComponent(qss::ControllerHand::Right, qss::ScalarSemantic::JoystickX));
	state.ConfigureComponent(3, MakeComponent(qss::ControllerHand::Right, qss::ScalarSemantic::JoystickY));

	state.ConsumeSample({100, 1, 0, 0.25F, 0.25F, 0.0, 0});
	state.ConsumeSample({101, 2, 1, -0.5F, -0.5F, 0.0, 0});
	state.ConsumeSample({102, 3, 2, 0.75F, 0.75F, 0.0, 0});
	state.ConsumeSample({103, 4, 3, -1.0F, -1.0F, 0.0, 0});

	EXPECT_TRUE(state.GetLeft().x.available);
	EXPECT_TRUE(state.GetLeft().y.available);
	EXPECT_TRUE(state.GetRight().x.available);
	EXPECT_TRUE(state.GetRight().y.available);
	EXPECT_FLOAT_EQ(state.GetLeft().x.rawValue, 0.25F);
	EXPECT_FLOAT_EQ(state.GetLeft().y.rawValue, -0.5F);
	EXPECT_FLOAT_EQ(state.GetRight().x.rawValue, 0.75F);
	EXPECT_FLOAT_EQ(state.GetRight().y.rawValue, -1.0F);
}

TEST(LiveStateTests, UnknownComponentsAreNotGuessed) {
	qss::SteamVRLiveState state;
	state.ConfigureComponent(0, MakeComponent(qss::ControllerHand::Unknown, qss::ScalarSemantic::JoystickX));
	state.ConfigureComponent(1, MakeComponent(qss::ControllerHand::Left, qss::ScalarSemantic::Unknown));

	state.ConsumeSample({100, 1, 0, 0.5F, 0.5F, 0.0, 0});
	state.ConsumeSample({101, 2, 1, 0.6F, 0.6F, 0.0, 0});

	EXPECT_FALSE(state.GetLeft().x.available);
	EXPECT_FALSE(state.GetLeft().y.available);
	EXPECT_FALSE(state.GetRight().x.available);
	EXPECT_FALSE(state.GetRight().y.available);
}

TEST(LiveStateTests, ReclassifiesAnExistingComponent) {
	qss::SteamVRLiveState state;
	state.ConfigureComponent(
		0,
		MakeComponent(qss::ControllerHand::Unknown, qss::ScalarSemantic::Unknown)
	);
	state.ConsumeSample({100, 1, 0, 0.5F, 0.5F, 0.0, 0});
	EXPECT_FALSE(state.GetRight().x.available);

	state.ConfigureComponent(
		0,
		MakeComponent(qss::ControllerHand::Right, qss::ScalarSemantic::JoystickX)
	);
	state.ConsumeSample({101, 2, 0, 0.75F, 0.75F, 0.0, 0});

	EXPECT_TRUE(state.GetRight().x.available);
	EXPECT_FLOAT_EQ(state.GetRight().x.rawValue, 0.75F);
}

TEST(LiveStateTests, CountsSequenceGaps) {
	qss::SteamVRLiveState state;
	state.ConsumeSample({100, 10, 0, 0.0F, 0.0F, 0.0, 0});
	state.ConsumeSample({101, 13, 0, 0.0F, 0.0F, 0.0, 0});

	EXPECT_EQ(state.GetLastSequence(), 13U);
	EXPECT_EQ(state.GetDroppedSampleCount(), 2U);
}

} // namespace
