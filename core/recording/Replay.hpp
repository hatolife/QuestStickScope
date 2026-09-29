#pragma once

#include "core/correction/Correction.hpp"
#include "core/recording/Recording.hpp"

namespace qss {

struct ReplayCorrectionSettings {
	CorrectionSettings left{};
	CorrectionSettings right{};
};

void RecalculateRecordingOutputs(
	RecordingData& recording,
	const ReplayCorrectionSettings& settings
);

} // namespace qss
