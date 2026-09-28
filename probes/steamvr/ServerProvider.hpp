#pragma once

#ifdef _WIN32

#include "platform/windows/SteamVRSharedMemory.hpp"
#include "probes/steamvr/DriverInputHooks.hpp"

#include <openvr_driver.h>

namespace qss {

class ServerProvider final : public vr::IServerTrackedDeviceProvider {
public:
	vr::EVRInitError Init(vr::IVRDriverContext* driverContext) override;
	void Cleanup() override;
	const char* const* GetInterfaceVersions() override;
	void RunFrame() override;
	bool ShouldBlockStandbyMode() override;
	void EnterStandby() override;
	void LeaveStandby() override;

private:
	SteamVRSharedMemoryWriter m_sharedMemory;
	DriverInputHooks m_hooks;
};

} // namespace qss

#endif
