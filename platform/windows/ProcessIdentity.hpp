#pragma once

#ifdef _WIN32

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace qss {

struct ProcessIdentity {
	bool running = false;
	std::uint32_t processId = 0;
	std::filesystem::path executablePath;
	std::string fileVersion;
	std::string sha256;
	std::string error;
};

ProcessIdentity InspectProcessIdentity(
	const std::vector<std::wstring>& executableNames
);

} // namespace qss

#endif
