#ifdef _WIN32

#include "probes/steamvr/ServerProvider.hpp"

#include <openvr_driver.h>

#include <cstring>

namespace {

qss::ServerProvider g_serverProvider;

} // namespace

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* interfaceName, int* returnCode) {
	if (interfaceName != nullptr && std::strcmp(vr::IServerTrackedDeviceProvider_Version, interfaceName) == 0) {
		return &g_serverProvider;
	}

	if (returnCode != nullptr) {
		*returnCode = vr::VRInitError_Init_InterfaceNotFound;
	}
	return nullptr;
}

#endif
