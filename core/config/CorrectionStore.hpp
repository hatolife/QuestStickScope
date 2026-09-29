#pragma once

#include "ipc/SharedProtocol.hpp"

#include <filesystem>
#include <string>

namespace qss {

SharedHandCorrection MakeDefaultSharedCorrection();

bool SaveCorrectionSettings(
	const std::filesystem::path& path,
	const SharedHandCorrection& left,
	const SharedHandCorrection& right,
	std::string* errorMessage = nullptr
);

bool LoadCorrectionSettings(
	const std::filesystem::path& path,
	SharedHandCorrection& left,
	SharedHandCorrection& right,
	std::string* errorMessage = nullptr
);

} // namespace qss
