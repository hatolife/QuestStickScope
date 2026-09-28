#include "probes/steamvr/ServerProvider.hpp"

#ifdef _WIN32

#include "platform/Clock.hpp"

namespace qss {
namespace {

void Log(const char* message) noexcept {
	if (vr::VRDriverLog() != nullptr) {
		vr::VRDriverLog()->Log(message);
	}
}

} // namespace

vr::EVRInitError ServerProvider::Init(vr::IVRDriverContext* driverContext) {
	VR_INIT_SERVER_DRIVER_CONTEXT(driverContext);
	Log("QuestStickScope: SteamVR Probe initializing.");

	if (!m_sharedMemory.Open()) {
		Log("QuestStickScope: shared memory unavailable; staying pass-through.");
		return vr::VRInitError_None;
	}

	m_sharedMemory.SetProbeState(ProbeState::PassThrough);
	vr::IVRDriverInput* driverInput = vr::VRDriverInput();
	if (driverInput == nullptr) {
		m_sharedMemory.SetProbeState(ProbeState::Error);
		Log("QuestStickScope: IVRDriverInput is unavailable; staying pass-through.");
		return vr::VRInitError_None;
	}

	if (!m_hooks.Install(driverInput, &m_sharedMemory)) {
		m_sharedMemory.SetProbeState(ProbeState::Error);
		Log("QuestStickScope: failed to install IVRDriverInput observation hooks; staying pass-through.");
		return vr::VRInitError_None;
	}

	m_sharedMemory.SetProbeState(ProbeState::Observing);
	Log("QuestStickScope: SteamVR Probe observation hooks active.");
	return vr::VRInitError_None;
}

void ServerProvider::Cleanup() {
	m_hooks.Remove();
	m_sharedMemory.SetProbeState(ProbeState::Offline);
	m_sharedMemory.Close();
	Log("QuestStickScope: SteamVR Probe cleanup complete.");
	VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* ServerProvider::GetInterfaceVersions() {
	return vr::k_InterfaceVersions;
}

void ServerProvider::RunFrame() {
	if (m_sharedMemory.IsOpen()) {
		m_sharedMemory.SetHeartbeat(MonotonicClock::NowTicks());
	}
}

bool ServerProvider::ShouldBlockStandbyMode() {
	return false;
}

void ServerProvider::EnterStandby() {
}

void ServerProvider::LeaveStandby() {
}

} // namespace qss

#endif
