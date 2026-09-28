#include "probes/steamvr/DriverInputHooks.hpp"

#ifdef _WIN32

#include "platform/Clock.hpp"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

namespace qss {
namespace {

using CreateScalarComponentFn = vr::EVRInputError(*)(
	vr::IVRDriverInput*,
	vr::PropertyContainerHandle_t,
	const char*,
	vr::VRInputComponentHandle_t*,
	vr::EVRScalarType,
	vr::EVRScalarUnits
);

using UpdateScalarComponentFn = vr::EVRInputError(*)(
	vr::IVRDriverInput*,
	vr::VRInputComponentHandle_t,
	float,
	double
);

struct ComponentBinding {
	std::atomic<std::uint64_t> handle{0};
	std::uint32_t sharedIndex = 0;
};

std::array<ComponentBinding, kSteamVRMaxScalarComponents> g_componentBindings{};
SteamVRSharedMemoryWriter* g_sharedMemory = nullptr;
CreateScalarComponentFn g_originalCreateScalar = nullptr;
UpdateScalarComponentFn g_originalUpdateScalar = nullptr;
void* g_createScalarTarget = nullptr;
void* g_updateScalarTarget = nullptr;
bool g_minhookInitialized = false;
bool g_hooksInstalled = false;

void ClearBindings() noexcept {
	for (ComponentBinding& binding : g_componentBindings) {
		binding.sharedIndex = 0;
		binding.handle.store(0, std::memory_order_relaxed);
	}
}

bool AddBinding(std::uint64_t handle, std::uint32_t sharedIndex) noexcept {
	for (ComponentBinding& binding : g_componentBindings) {
		std::uint64_t expected = 0;
		if (binding.handle.compare_exchange_strong(
			expected,
			std::numeric_limits<std::uint64_t>::max(),
			std::memory_order_acq_rel,
			std::memory_order_relaxed
		)) {
			binding.sharedIndex = sharedIndex;
			binding.handle.store(handle, std::memory_order_release);
			return true;
		}
	}
	return false;
}

bool FindBinding(std::uint64_t handle, std::uint32_t& sharedIndex) noexcept {
	for (const ComponentBinding& binding : g_componentBindings) {
		if (binding.handle.load(std::memory_order_acquire) == handle) {
			sharedIndex = binding.sharedIndex;
			return true;
		}
	}
	return false;
}

ControllerHand DetectHand(vr::PropertyContainerHandle_t container) noexcept {
	vr::ETrackedPropertyError error = vr::TrackedProp_Success;
	const std::int32_t role = vr::VRProperties()->GetInt32Property(
		container,
		vr::Prop_ControllerRoleHint_Int32,
		&error
	);
	if (error != vr::TrackedProp_Success) {
		return ControllerHand::Unknown;
	}
	if (role == vr::TrackedControllerRole_LeftHand) {
		return ControllerHand::Left;
	}
	if (role == vr::TrackedControllerRole_RightHand) {
		return ControllerHand::Right;
	}
	return ControllerHand::Unknown;
}

ScalarSemantic DetectSemantic(const char* path) noexcept {
	if (path == nullptr) {
		return ScalarSemantic::Unknown;
	}
	if (std::strcmp(path, "/input/joystick/x") == 0 || std::strcmp(path, "/input/thumbstick/x") == 0) {
		return ScalarSemantic::JoystickX;
	}
	if (std::strcmp(path, "/input/joystick/y") == 0 || std::strcmp(path, "/input/thumbstick/y") == 0) {
		return ScalarSemantic::JoystickY;
	}
	return ScalarSemantic::Unknown;
}

vr::EVRInputError HookCreateScalarComponent(
	vr::IVRDriverInput* self,
	vr::PropertyContainerHandle_t container,
	const char* path,
	vr::VRInputComponentHandle_t* handle,
	vr::EVRScalarType scalarType,
	vr::EVRScalarUnits scalarUnits
) {
	const vr::EVRInputError result = g_originalCreateScalar(
		self,
		container,
		path,
		handle,
		scalarType,
		scalarUnits
	);

	if (result != vr::VRInputError_None || handle == nullptr || g_sharedMemory == nullptr) {
		return result;
	}

	std::uint32_t sharedIndex = 0;
	if (g_sharedMemory->RegisterScalarComponent(
		static_cast<std::uint64_t>(*handle),
		static_cast<std::uint64_t>(container),
		path,
		static_cast<std::int32_t>(scalarType),
		static_cast<std::int32_t>(scalarUnits),
		DetectHand(container),
		DetectSemantic(path),
		sharedIndex
	)) {
		AddBinding(static_cast<std::uint64_t>(*handle), sharedIndex);
	}

	return result;
}

vr::EVRInputError HookUpdateScalarComponent(
	vr::IVRDriverInput* self,
	vr::VRInputComponentHandle_t component,
	float newValue,
	double timeOffset
) {
	const std::int64_t timestampTicks = MonotonicClock::NowTicks();
	const vr::EVRInputError result = g_originalUpdateScalar(self, component, newValue, timeOffset);

	if (g_sharedMemory == nullptr) {
		return result;
	}

	std::uint32_t sharedIndex = 0;
	if (!FindBinding(static_cast<std::uint64_t>(component), sharedIndex)) {
		return result;
	}

	SharedScalarSample sample{};
	sample.timestampTicks = timestampTicks;
	sample.componentIndex = sharedIndex;
	sample.rawValue = newValue;
	sample.outputValue = newValue;
	sample.timeOffset = timeOffset;
	g_sharedMemory->WriteScalarSample(sample);
	return result;
}

bool InitializeMinHook() noexcept {
	const MH_STATUS status = MH_Initialize();
	if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
		g_minhookInitialized = true;
		return true;
	}
	return false;
}

void RemoveHooks() noexcept {
	if (g_hooksInstalled) {
		MH_DisableHook(MH_ALL_HOOKS);
		if (g_createScalarTarget != nullptr) {
			MH_RemoveHook(g_createScalarTarget);
		}
		if (g_updateScalarTarget != nullptr) {
			MH_RemoveHook(g_updateScalarTarget);
		}
		g_hooksInstalled = false;
	}

	if (g_minhookInitialized) {
		MH_Uninitialize();
		g_minhookInitialized = false;
	}

	g_createScalarTarget = nullptr;
	g_updateScalarTarget = nullptr;
	g_originalCreateScalar = nullptr;
	g_originalUpdateScalar = nullptr;
	g_sharedMemory = nullptr;
	ClearBindings();
}

} // namespace

bool DriverInputHooks::Install(vr::IVRDriverInput* driverInput, SteamVRSharedMemoryWriter* sharedMemory) noexcept {
	RemoveHooks();
	if (driverInput == nullptr || sharedMemory == nullptr || !sharedMemory->IsOpen()) {
		return false;
	}

	void** vtable = *reinterpret_cast<void***>(driverInput);
	if (vtable == nullptr) {
		return false;
	}

	g_createScalarTarget = vtable[2];
	g_updateScalarTarget = vtable[3];
	g_sharedMemory = sharedMemory;

	if (!InitializeMinHook()) {
		RemoveHooks();
		return false;
	}

	if (MH_CreateHook(
		g_createScalarTarget,
		reinterpret_cast<void*>(&HookCreateScalarComponent),
		reinterpret_cast<void**>(&g_originalCreateScalar)
	) != MH_OK) {
		RemoveHooks();
		return false;
	}

	if (MH_CreateHook(
		g_updateScalarTarget,
		reinterpret_cast<void*>(&HookUpdateScalarComponent),
		reinterpret_cast<void**>(&g_originalUpdateScalar)
	) != MH_OK) {
		RemoveHooks();
		return false;
	}

	if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
		RemoveHooks();
		return false;
	}

	g_hooksInstalled = true;
	return true;
}

void DriverInputHooks::Remove() noexcept {
	RemoveHooks();
}

bool DriverInputHooks::IsInstalled() const noexcept {
	return g_hooksInstalled;
}

} // namespace qss

#endif
