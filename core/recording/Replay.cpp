#include "core/recording/Replay.hpp"

#include <array>
#include <cstddef>

namespace qss {
namespace {

struct RawStick {
	Vec2 value{};
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

	RawStick left;
	RawStick right;
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

		if (target.semantic == ScalarSemantic::JoystickX) {
			sample.outputValue = result.output.x;
		} else {
			sample.outputValue = result.output.y;
		}
		sample.flags |= kSampleFlagCorrectionApplied;
	}
}

} // namespace qss
