#include "core/recording/Replay.hpp"

#include <array>
#include <cstddef>

namespace qss {
namespace {

struct RawStick {
	Vec2 value{};
};

struct SmoothingState {
	float value = 0.0F;
	std::int64_t timestampTicks = 0;
	bool initialized = false;
};

struct HandSmoothingState {
	SmoothingState x{};
	SmoothingState y{};
};

struct ComponentTarget {
	ControllerHand hand = ControllerHand::Unknown;
	ScalarSemantic semantic = ScalarSemantic::Unknown;
};

const CorrectionSettings* ResolveSettings(
	ControllerHand hand,
	const ReplayCorrectionSettings& settings
) {
	if (hand == ControllerHand::Left) {
		return &settings.left;
	}
	if (hand == ControllerHand::Right) {
		return &settings.right;
	}
	return nullptr;
}

RawStick* ResolveRawStick(
	ControllerHand hand,
	RawStick& left,
	RawStick& right
) {
	if (hand == ControllerHand::Left) {
		return &left;
	}
	if (hand == ControllerHand::Right) {
		return &right;
	}
	return nullptr;
}

SmoothingState* ResolveSmoothingState(
	ControllerHand hand,
	ScalarSemantic semantic,
	HandSmoothingState& left,
	HandSmoothingState& right
) {
	HandSmoothingState* handState = nullptr;
	if (hand == ControllerHand::Left) {
		handState = &left;
	} else if (hand == ControllerHand::Right) {
		handState = &right;
	}
	if (handState == nullptr) {
		return nullptr;
	}
	if (semantic == ScalarSemantic::JoystickX) {
		return &handState->x;
	}
	if (semantic == ScalarSemantic::JoystickY) {
		return &handState->y;
	}
	return nullptr;
}

} // namespace

void RecalculateRecordingOutputs(
	RecordingData& recording,
	const ReplayCorrectionSettings& settings
) {
	std::vector<ComponentTarget> targets(recording.components.size());
	for (std::size_t index = 0; index < recording.components.size(); ++index) {
		targets[index].hand = recording.components[index].hand;
		targets[index].semantic = recording.components[index].semantic;
	}

	RawStick left{{recording.initialLeftX, recording.initialLeftY}};
	RawStick right{{recording.initialRightX, recording.initialRightY}};
	HandSmoothingState leftSmoothing;
	HandSmoothingState rightSmoothing;
	for (SharedScalarSample& sample : recording.samples) {
		sample.outputValue = sample.rawValue;
		sample.flags &= ~kSampleFlagCorrectionApplied;

		if (sample.componentIndex >= targets.size()) {
			continue;
		}

		const ComponentTarget target = targets[sample.componentIndex];
		RawStick* rawStick = ResolveRawStick(target.hand, left, right);
		const CorrectionSettings* correction = ResolveSettings(target.hand, settings);
		if (rawStick == nullptr || correction == nullptr) {
			continue;
		}

		if (target.semantic == ScalarSemantic::JoystickX) {
			rawStick->value.x = sample.rawValue;
		} else if (target.semantic == ScalarSemantic::JoystickY) {
			rawStick->value.y = sample.rawValue;
		} else {
			continue;
		}

		const CorrectionResult result = ApplyCorrection(rawStick->value, *correction);
		if (!correction->enabled) {
			continue;
		}

		const float correctedValue = target.semantic == ScalarSemantic::JoystickX
			? result.output.x
			: result.output.y;
		SmoothingState* smoothingState = ResolveSmoothingState(
			target.hand,
			target.semantic,
			leftSmoothing,
			rightSmoothing
		);
		float outputValue = correctedValue;
		if (smoothingState != nullptr) {
			if (smoothingState->initialized) {
				outputValue = ApplySmoothing(
					correctedValue,
					smoothingState->value,
					correction->smoothing,
					sample.timestampTicks - smoothingState->timestampTicks,
					static_cast<std::int64_t>(recording.qpcFrequency)
				);
			}
			smoothingState->value = outputValue;
			smoothingState->timestampTicks = sample.timestampTicks;
			smoothingState->initialized = true;
		}
		sample.outputValue = outputValue;
		sample.flags |= kSampleFlagCorrectionApplied;
	}
}

} // namespace qss
