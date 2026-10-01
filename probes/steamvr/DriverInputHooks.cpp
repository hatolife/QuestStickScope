#include "probes/steamvr/DriverInputHooks.hpp"

#ifdef _WIN32

#include "core/correction/Correction.hpp"
#include "platform/Clock.hpp"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>

namespace qss {
namespace {

static_assert(kCorrectionDirectionCount == kSharedDirectionCount);

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
	std::atomic<std::uint32_t> sharedIndex{0};
	std::atomic<std::uint32_t> hand{static_cast<std::uint32_t>(ControllerHand::Unknown)};
	std::atomic<std::uint32_t> semantic{static_cast<std::uint32_t>(ScalarSemantic::Unknown)};
};

struct ComponentBindingSnapshot {
	std::uint32_t sharedIndex = 0;
	ControllerHand hand = ControllerHand::Unknown;
	ScalarSemantic semantic = ScalarSemantic::Unknown;
};

struct RawStickState {
	std::atomic<float> x{0.0F};
	std::atomic<float> y{0.0F};
};

struct ScalarSmoothingState {
	std::atomic<float> value{0.0F};
	std::atomic<std::int64_t> timestampTicks{0};
};

std::array<ComponentBinding, kSteamVRMaxScalarComponents> g_componentBindings{};
std::mutex g_bindingMutationMutex;
RawStickState g_leftRaw{};
RawStickState g_rightRaw{};
ScalarSmoothingState g_leftSmoothingX{};
ScalarSmoothingState g_leftSmoothingY{};
ScalarSmoothingState g_rightSmoothingX{};
ScalarSmoothingState g_rightSmoothingY{};
SteamVRSharedMemoryWriter* g_sharedMemory = nullptr;
CreateScalarComponentFn g_originalCreateScalar = nullptr;
UpdateScalarComponentFn g_originalUpdateScalar = nullptr;
void* g_createScalarTarget = nullptr;
void* g_updateScalarTarget = nullptr;
bool g_minhookInitialized = false;
bool g_hooksInstalled = false;

void ClearBindings() noexcept {
	for (ComponentBinding& binding : g_componentBindings) {
		binding.sharedIndex.store(0, std::memory_order_relaxed);
		binding.hand.store(
			static_cast<std::uint32_t>(ControllerHand::Unknown),
			std::memory_order_relaxed
		);
		binding.semantic.store(
			static_cast<std::uint32_t>(ScalarSemantic::Unknown),
			std::memory_order_relaxed
		);
		binding.handle.store(0, std::memory_order_relaxed);
	}
}

bool AddBinding(
	std::uint64_t handle,
	std::uint32_t sharedIndex,
	ControllerHand hand,
	ScalarSemantic semantic
) noexcept {
	for (ComponentBinding& binding : g_componentBindings) {
		if (binding.handle.load(std::memory_order_acquire) == 0) {
			binding.sharedIndex.store(sharedIndex, std::memory_order_relaxed);
			binding.hand.store(
				static_cast<std::uint32_t>(hand),
				std::memory_order_relaxed
			);
			binding.semantic.store(
				static_cast<std::uint32_t>(semantic),
				std::memory_order_relaxed
			);
			binding.handle.store(handle, std::memory_order_release);
			return true;
		}
	}
	return false;
}

bool FindBinding(std::uint64_t handle, ComponentBindingSnapshot& output) noexcept {
	for (const ComponentBinding& binding : g_componentBindings) {
		if (binding.handle.load(std::memory_order_acquire) == handle) {
			output.sharedIndex = binding.sharedIndex.load(std::memory_order_relaxed);
			output.hand = static_cast<ControllerHand>(
				binding.hand.load(std::memory_order_relaxed)
			);
			output.semantic = static_cast<ScalarSemantic>(
				binding.semantic.load(std::memory_order_relaxed)
			);
			return true;
		}
	}
	return false;
}

bool FindOrRegisterUnknownBinding(
	std::uint64_t handle,
	ComponentBindingSnapshot& output
) noexcept {
	if (FindBinding(handle, output)) {
		return true;
	}
	if (g_sharedMemory == nullptr) {
		return false;
	}

	std::lock_guard<std::mutex> lock(g_bindingMutationMutex);
	if (FindBinding(handle, output)) {
		return true;
	}

	char path[kSteamVRComponentPathCapacity]{};
	std::snprintf(
		path,
		sizeof(path),
		"<unknown:%llu>",
		static_cast<unsigned long long>(handle)
	);

	std::uint32_t sharedIndex = 0;
	if (!g_sharedMemory->RegisterScalarComponent(
		handle,
		0,
		path,
		0,
		0,
		ControllerHand::Unknown,
		ScalarSemantic::Unknown,
		sharedIndex
	)) {
		return false;
	}
	if (!AddBinding(
		handle,
		sharedIndex,
		ControllerHand::Unknown,
		ScalarSemantic::Unknown
	)) {
		return FindBinding(handle, output);
	}

	output.sharedIndex = sharedIndex;
	output.hand = ControllerHand::Unknown;
	output.semantic = ScalarSemantic::Unknown;
	return true;
}

RawStickState* GetRawStick(ControllerHand hand) noexcept {
	if (hand == ControllerHand::Left) {
		return &g_leftRaw;
	}
	if (hand == ControllerHand::Right) {
		return &g_rightRaw;
	}
	return nullptr;
}

CorrectionSettings ToCorrectionSettings(const SharedHandCorrection& shared) {
	CorrectionSettings settings;
	settings.enabled = shared.enabled != 0;
	settings.centerOffsetEnabled = shared.centerOffsetEnabled != 0;
	settings.innerDeadzoneEnabled = shared.innerDeadzoneEnabled != 0;
	settings.outerNormalizationEnabled = shared.outerNormalizationEnabled != 0;
	settings.clampEnabled = shared.clampEnabled != 0;
	settings.center = {shared.centerX, shared.centerY};
	settings.innerDeadzone = shared.innerDeadzone;
	settings.outerScale = shared.outerScale;
	settings.responseCurve = shared.responseCurve;
	settings.smoothing = shared.smoothing;
	settings.innerRadius = shared.innerRadius;
	settings.outerRadius = shared.outerRadius;
	return settings;
}

bool IsClientAlive(std::int64_t nowTicks) noexcept {
	if (g_sharedMemory == nullptr) {
		return false;
	}
	const std::uint64_t heartbeat = g_sharedMemory->GetClientHeartbeatTicks();
	if (heartbeat == 0 || nowTicks < 0 || static_cast<std::uint64_t>(nowTicks) < heartbeat) {
		return false;
	}
	const std::uint64_t timeout = static_cast<std::uint64_t>(MonotonicClock::Frequency()) * 2ULL;
	return static_cast<std::uint64_t>(nowTicks) - heartbeat <= timeout;
}

ScalarSmoothingState* GetSmoothingState(
	ControllerHand hand,
	ScalarSemantic semantic
) noexcept {
	if (hand == ControllerHand::Left) {
		if (semantic == ScalarSemantic::JoystickX) {
			return &g_leftSmoothingX;
		}
		if (semantic == ScalarSemantic::JoystickY) {
			return &g_leftSmoothingY;
		}
	}
	if (hand == ControllerHand::Right) {
		if (semantic == ScalarSemantic::JoystickX) {
			return &g_rightSmoothingX;
		}
		if (semantic == ScalarSemantic::JoystickY) {
			return &g_rightSmoothingY;
		}
	}
	return nullptr;
}

void ResetSmoothingState(
	const ComponentBindingSnapshot& binding,
	float value,
	std::int64_t timestampTicks
) noexcept {
	ScalarSmoothingState* state = GetSmoothingState(binding.hand, binding.semantic);
	if (state == nullptr) {
		return;
	}
	state->value.store(value, std::memory_order_relaxed);
	state->timestampTicks.store(timestampTicks, std::memory_order_relaxed);
}

float ApplyConfiguredSmoothing(
	const ComponentBindingSnapshot& binding,
	float value,
	float smoothing,
	std::int64_t nowTicks
) noexcept {
	ScalarSmoothingState* state = GetSmoothingState(binding.hand, binding.semantic);
	if (state == nullptr) {
		return value;
	}

	const std::int64_t previousTicks = state->timestampTicks.exchange(
		nowTicks,
		std::memory_order_relaxed
	);
	const float previousValue = state->value.load(std::memory_order_relaxed);
	float output = value;
	if (previousTicks > 0 && nowTicks > previousTicks) {
		output = ApplySmoothing(
			value,
			previousValue,
			smoothing,
			nowTicks - previousTicks,
			MonotonicClock::Frequency()
		);
	}
	state->value.store(output, std::memory_order_relaxed);
	return output;
}

float ApplyConfiguredCorrection(
	const ComponentBindingSnapshot& binding,
	float rawValue,
	std::int64_t nowTicks,
	bool& correctionApplied
) {
	correctionApplied = false;
	RawStickState* rawStick = GetRawStick(binding.hand);
	if (rawStick == nullptr) {
		return rawValue;
	}

	if (binding.semantic == ScalarSemantic::JoystickX) {
		rawStick->x.store(rawValue, std::memory_order_relaxed);
	} else if (binding.semantic == ScalarSemantic::JoystickY) {
		rawStick->y.store(rawValue, std::memory_order_relaxed);
	} else {
		return rawValue;
	}

	if (!IsClientAlive(nowTicks)) {
		ResetSmoothingState(binding, rawValue, nowTicks);
		return rawValue;
	}

	CorrectionControlSnapshot control;
	if (!g_sharedMemory->ReadCorrectionControl(control)) {
		ResetSmoothingState(binding, rawValue, nowTicks);
		return rawValue;
	}

	const SharedHandCorrection& shared = binding.hand == ControllerHand::Left ? control.left : control.right;
	const CorrectionSettings settings = ToCorrectionSettings(shared);
	if (!settings.enabled) {
		ResetSmoothingState(binding, rawValue, nowTicks);
		return rawValue;
	}

	const Vec2 raw{
		rawStick->x.load(std::memory_order_relaxed),
		rawStick->y.load(std::memory_order_relaxed),
	};
	const CorrectionResult corrected = ApplyCorrection(raw, settings);
	const float correctedValue = binding.semantic == ScalarSemantic::JoystickX
		? corrected.output.x
		: corrected.output.y;
	correctionApplied = true;
	return ApplyConfiguredSmoothing(
		binding,
		correctedValue,
		settings.smoothing,
		nowTicks
	);
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

bool UpgradeBinding(
	std::uint64_t handle,
	std::uint64_t container,
	const char* path,
	std::int32_t scalarType,
	std::int32_t scalarUnits,
	ControllerHand hand,
	ScalarSemantic semantic
) noexcept {
	ComponentBindingSnapshot existing;
	if (!FindBinding(handle, existing)) {
		return false;
	}

	if (!g_sharedMemory->UpdateScalarComponent(
		existing.sharedIndex,
		handle,
		container,
		path,
		scalarType,
		scalarUnits,
		hand,
		semantic
	)) {
		return false;
	}

	for (ComponentBinding& binding : g_componentBindings) {
		if (binding.handle.load(std::memory_order_acquire) == handle) {
			binding.hand.store(
				static_cast<std::uint32_t>(hand),
				std::memory_order_release
			);
			binding.semantic.store(
				static_cast<std::uint32_t>(semantic),
				std::memory_order_release
			);
			return true;
		}
	}
	return false;
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

	const ControllerHand hand = DetectHand(container);
	const ScalarSemantic semantic = DetectSemantic(path);
	const std::uint64_t numericHandle = static_cast<std::uint64_t>(*handle);

	std::lock_guard<std::mutex> lock(g_bindingMutationMutex);
	if (UpgradeBinding(
		numericHandle,
		static_cast<std::uint64_t>(container),
		path,
		static_cast<std::int32_t>(scalarType),
		static_cast<std::int32_t>(scalarUnits),
		hand,
		semantic
	)) {
		return result;
	}

	std::uint32_t sharedIndex = 0;
	if (g_sharedMemory->RegisterScalarComponent(
		static_cast<std::uint64_t>(*handle),
		static_cast<std::uint64_t>(container),
		path,
		static_cast<std::int32_t>(scalarType),
		static_cast<std::int32_t>(scalarUnits),
		hand,
		semantic,
		sharedIndex
	)) {
		AddBinding(numericHandle, sharedIndex, hand, semantic);
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
	ComponentBindingSnapshot binding;
	const bool knownComponent = FindOrRegisterUnknownBinding(
		static_cast<std::uint64_t>(component),
		binding
	);

	bool correctionApplied = false;
	const float outputValue = knownComponent
		? ApplyConfiguredCorrection(binding, newValue, timestampTicks, correctionApplied)
		: newValue;
	const vr::EVRInputError result = g_originalUpdateScalar(self, component, outputValue, timeOffset);

	if (g_sharedMemory == nullptr || !knownComponent) {
		return result;
	}

	SharedScalarSample sample{};
	sample.timestampTicks = timestampTicks;
	sample.componentIndex = binding.sharedIndex;
	sample.rawValue = newValue;
	sample.outputValue = outputValue;
	sample.timeOffset = timeOffset;
	if (binding.hand == ControllerHand::Unknown &&
		binding.semantic == ScalarSemantic::Unknown) {
		sample.flags |= kSampleFlagUnknownComponent;
	}
	if (correctionApplied) {
		sample.flags |= kSampleFlagCorrectionApplied;
	}
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
	g_leftRaw.x.store(0.0F, std::memory_order_relaxed);
	g_leftRaw.y.store(0.0F, std::memory_order_relaxed);
	g_rightRaw.x.store(0.0F, std::memory_order_relaxed);
	g_rightRaw.y.store(0.0F, std::memory_order_relaxed);
	g_leftSmoothingX.value.store(0.0F, std::memory_order_relaxed);
	g_leftSmoothingX.timestampTicks.store(0, std::memory_order_relaxed);
	g_leftSmoothingY.value.store(0.0F, std::memory_order_relaxed);
	g_leftSmoothingY.timestampTicks.store(0, std::memory_order_relaxed);
	g_rightSmoothingX.value.store(0.0F, std::memory_order_relaxed);
	g_rightSmoothingX.timestampTicks.store(0, std::memory_order_relaxed);
	g_rightSmoothingY.value.store(0.0F, std::memory_order_relaxed);
	g_rightSmoothingY.timestampTicks.store(0, std::memory_order_relaxed);
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
