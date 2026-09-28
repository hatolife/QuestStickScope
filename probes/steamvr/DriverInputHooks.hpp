#pragma once

#ifdef _WIN32

#include "platform/windows/SteamVRSharedMemory.hpp"

#include <openvr_driver.h>

namespace qss {

class DriverInputHooks {
public:
	bool Install(vr::IVRDriverInput* driverInput, SteamVRSharedMemoryWriter* sharedMemory) noexcept;
	void Remove() noexcept;
	bool IsInstalled() const noexcept;
};

} // namespace qss

#endif
