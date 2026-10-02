#pragma once

#ifdef _WIN32

#include <filesystem>
#include <string>

namespace qss {

struct SteamVRDriverRegistrationResult {
	bool success = false;
	bool changed = false;
	std::filesystem::path driverRoot;
	std::filesystem::path registeredDriverRoot;
	std::string message;
};

SteamVRDriverRegistrationResult EnsureSteamVRDriverRegistration();

} // namespace qss

#endif
