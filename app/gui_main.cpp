#ifdef _WIN32

#include "core/analysis/StickStatistics.hpp"
#include "core/analysis/WindowsInputAnalysis.hpp"
#include "core/calibration/Calibration.hpp"
#include "core/config/CorrectionStore.hpp"
#include "core/live/SteamVRLiveState.hpp"
#include "core/recording/Recording.hpp"
#include "core/recording/Replay.hpp"
#include "platform/Clock.hpp"
#include "platform/windows/SteamVRSharedMemory.hpp"
#include "platform/windows/ProcessIdentity.hpp"
#include "platform/windows/WindowsMouseObserver.hpp"
#include "platform/windows/WindowsRawInputObserver.hpp"
#include "platform/windows/WindowsXInputObserver.hpp"

#include <d3d11.h>
#include <ShlObj.h>
#include <Windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <climits>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_deviceContext = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;
UINT g_resizeWidth = 0;
UINT g_resizeHeight = 0;
bool g_swapChainOccluded = false;

std::filesystem::path GetDataDirectory() {
	PWSTR localAppData = nullptr;
	if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) {
		return {};
	}
	const std::filesystem::path directory =
		std::filesystem::path(localAppData) / L"QuestStickScope";
	::CoTaskMemFree(localAppData);
	return directory;
}

std::filesystem::path GetRecordingDirectory() {
	const std::filesystem::path dataDirectory = GetDataDirectory();
	return dataDirectory.empty() ? std::filesystem::path{} : dataDirectory / L"recordings";
}

std::filesystem::path GetCalibrationPath() {
	const std::filesystem::path dataDirectory = GetDataDirectory();
	return dataDirectory.empty() ? std::filesystem::path{} : dataDirectory / L"calibration.json";
}

std::filesystem::path MakeRecordingFilePath() {
	const std::filesystem::path directory = GetRecordingDirectory();
	if (directory.empty()) {
		return {};
	}
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error) {
		return {};
	}

	SYSTEMTIME time{};
	::GetLocalTime(&time);
	wchar_t name[96]{};
	swprintf_s(
		name,
		L"steamvr-%04u%02u%02u-%02u%02u%02u.qssrec",
		time.wYear,
		time.wMonth,
		time.wDay,
		time.wHour,
		time.wMinute,
		time.wSecond
	);
	return directory / name;
}

std::string PathToUtf8(const std::filesystem::path& path) {
	const std::u8string value = path.u8string();
	return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

qss::SharedHandCorrection ToSharedCorrection(const qss::CorrectionSettings& settings) {
	qss::SharedHandCorrection shared;
	shared.enabled = settings.enabled ? 1U : 0U;
	shared.centerOffsetEnabled = settings.centerOffsetEnabled ? 1U : 0U;
	shared.innerDeadzoneEnabled = settings.innerDeadzoneEnabled ? 1U : 0U;
	shared.outerNormalizationEnabled = settings.outerNormalizationEnabled ? 1U : 0U;
	shared.clampEnabled = settings.clampEnabled ? 1U : 0U;
	shared.centerX = settings.center.x;
	shared.centerY = settings.center.y;
	shared.innerDeadzone = settings.innerDeadzone;
	shared.outerRadius = settings.outerRadius;
	return shared;
}

qss::CorrectionSettings ToCorrectionSettings(const qss::SharedHandCorrection& shared) {
	qss::CorrectionSettings settings;
	settings.enabled = shared.enabled != 0;
	settings.centerOffsetEnabled = shared.centerOffsetEnabled != 0;
	settings.innerDeadzoneEnabled = shared.innerDeadzoneEnabled != 0;
	settings.outerNormalizationEnabled = shared.outerNormalizationEnabled != 0;
	settings.clampEnabled = shared.clampEnabled != 0;
	settings.center = {shared.centerX, shared.centerY};
	settings.innerDeadzone = shared.innerDeadzone;
	settings.outerRadius = shared.outerRadius;
	return settings;
}

qss::WindowsInputKind ToWindowsInputKind(qss::WindowsMouseEventType type) {
	switch (type) {
	case qss::WindowsMouseEventType::Move:
		return qss::WindowsInputKind::Move;
	case qss::WindowsMouseEventType::Wheel:
		return qss::WindowsInputKind::Wheel;
	case qss::WindowsMouseEventType::HorizontalWheel:
		return qss::WindowsInputKind::HorizontalWheel;
	case qss::WindowsMouseEventType::LeftDown:
	case qss::WindowsMouseEventType::LeftUp:
	case qss::WindowsMouseEventType::RightDown:
	case qss::WindowsMouseEventType::RightUp:
	case qss::WindowsMouseEventType::MiddleDown:
	case qss::WindowsMouseEventType::MiddleUp:
	case qss::WindowsMouseEventType::XButtonDown:
	case qss::WindowsMouseEventType::XButtonUp:
		return qss::WindowsInputKind::Button;
	case qss::WindowsMouseEventType::Unknown:
		return qss::WindowsInputKind::Unknown;
	}
	return qss::WindowsInputKind::Unknown;
}

qss::WindowsInputSample ToRecordingSample(const qss::WindowsMouseEvent& event) {
	qss::WindowsInputSample sample;
	sample.timestampTicks = event.timestampTicks;
	sample.source = qss::WindowsInputSource::LowLevelMouse;
	sample.kind = ToWindowsInputKind(event.type);
	sample.x = event.x;
	sample.y = event.y;
	sample.wheelDelta = event.wheelDelta;
	sample.flags = event.flags;
	sample.extraInfo = static_cast<std::uint64_t>(event.extraInfo);
	return sample;
}

qss::WindowsInputSample ToRecordingSample(const qss::WindowsRawMouseEvent& event) {
	qss::WindowsInputSample sample;
	sample.timestampTicks = event.timestampTicks;
	sample.source = qss::WindowsInputSource::RawInputMouse;
	if ((event.buttonFlags & RI_MOUSE_WHEEL) != 0) {
		sample.kind = qss::WindowsInputKind::Wheel;
	} else if ((event.buttonFlags & RI_MOUSE_HWHEEL) != 0) {
		sample.kind = qss::WindowsInputKind::HorizontalWheel;
	} else if (event.buttonFlags != 0) {
		sample.kind = qss::WindowsInputKind::Button;
	} else {
		sample.kind = qss::WindowsInputKind::Move;
	}
	sample.deltaX = event.deltaX;
	sample.deltaY = event.deltaY;
	sample.wheelDelta = event.wheelDelta;
	sample.flags =
		static_cast<std::uint32_t>(event.mouseFlags) |
		(static_cast<std::uint32_t>(event.buttonFlags) << 16);
	sample.device = static_cast<std::uint64_t>(event.device);
	return sample;
}

enum class CalibrationMode {
	None,
	Center,
	Outer,
};

struct GuiState {
	qss::SteamVRSharedMemoryReader reader;
	qss::SteamVRSharedMemoryController controller;
	qss::SteamVRLiveState live;
	std::vector<qss::ScalarComponentSnapshot> components;
	std::vector<qss::SharedScalarSample> latestComponentSamples;
	std::vector<bool> componentHasSample;
	std::deque<qss::StickPointSample> leftHistory;
	std::deque<qss::StickPointSample> rightHistory;
	qss::WindowsMouseObserver windowsMouseObserver;
	std::deque<qss::WindowsMouseEvent> windowsMouseEvents;
	qss::WindowsRawInputObserver windowsRawInputObserver;
	std::deque<qss::WindowsRawMouseEvent> windowsRawMouseEvents;
	qss::WindowsXInputObserver windowsXInputObserver;
	qss::ProcessIdentity steamVrIdentity;
	qss::ProcessIdentity virtualDesktopIdentity;
	bool processIdentityScanned = false;
	qss::SharedHandCorrection leftCorrection{};
	qss::SharedHandCorrection rightCorrection{};
	std::uint64_t sessionId = 0;
	std::uint64_t nextSequence = 0;
	std::uint32_t configuredComponentCount = 0;
	bool connected = false;
	bool correctionLoaded = false;
	bool correctionDirty = false;
	qss::RecordingData activeRecording;
	bool recordingActive = false;
	std::string recordingStatus;
	std::vector<std::filesystem::path> recordingFiles;
	int selectedRecordingIndex = -1;
	qss::RecordingData replayRecording;
	qss::SteamVRLiveState replayLive;
	std::size_t replayCursor = 0;
	std::size_t windowsReplayCursor = 0;
	bool replayLoaded = false;
	std::string replayStatus;
	CalibrationMode calibrationMode = CalibrationMode::None;
	qss::ControllerHand calibrationHand = qss::ControllerHand::Unknown;
	std::int64_t calibrationStartTicks = 0;
	std::vector<qss::Vec2> calibrationSamples;
	qss::CenterCalibrationResult leftCenterResult;
	qss::CenterCalibrationResult rightCenterResult;
	qss::OuterCalibrationResult leftOuterResult;
	qss::OuterCalibrationResult rightOuterResult;
	std::string calibrationStatus;
	bool persistentCorrectionAvailable = false;
	bool persistentCorrectionDirty = false;
	std::int64_t persistentCorrectionChangedTicks = 0;
	std::string persistenceStatus;

	explicit GuiState(HWND window) {
		windowsMouseObserver.Start();
		windowsRawInputObserver.Start(window);
		std::string error;
		const std::filesystem::path path = GetCalibrationPath();
		if (!path.empty() &&
			qss::LoadCorrectionSettings(path, leftCorrection, rightCorrection, &error)) {
			persistentCorrectionAvailable = true;
			persistenceStatus = "Loaded calibration.json.";
		} else {
			leftCorrection = qss::MakeDefaultSharedCorrection();
			rightCorrection = qss::MakeDefaultSharedCorrection();
			if (!error.empty() && std::filesystem::exists(path)) {
				persistenceStatus = "Ignored invalid calibration.json: " + error;
			}
		}
	}

	void RefreshProcessIdentities() {
		steamVrIdentity = qss::InspectProcessIdentity({L"vrserver.exe"});
		virtualDesktopIdentity = qss::InspectProcessIdentity({
			L"VirtualDesktop.Streamer.exe",
			L"VirtualDesktop.Streamer64.exe"
		});
		processIdentityScanned = true;
	}

	bool SavePersistentCorrection() {
		if (!persistentCorrectionDirty) {
			return true;
		}
		const std::filesystem::path path = GetCalibrationPath();
		if (path.empty()) {
			persistenceStatus = "Failed to resolve calibration.json path.";
			return false;
		}
		std::string error;
		if (!qss::SaveCorrectionSettings(
			path,
			leftCorrection,
			rightCorrection,
			&error
		)) {
			persistenceStatus = "Failed to save calibration.json: " + error;
			return false;
		}
		persistentCorrectionDirty = false;
		persistentCorrectionAvailable = true;
		persistenceStatus = "Saved calibration.json.";
		return true;
	}

	void StartCenterCalibration(qss::ControllerHand hand) {
		calibrationMode = CalibrationMode::Center;
		calibrationHand = hand;
		calibrationStartTicks = qss::MonotonicClock::NowTicks();
		calibrationSamples.clear();
		calibrationStatus = "Keep the stick released for 3 seconds.";
	}

	void StartOuterCalibration(qss::ControllerHand hand) {
		calibrationMode = CalibrationMode::Outer;
		calibrationHand = hand;
		calibrationStartTicks = qss::MonotonicClock::NowTicks();
		calibrationSamples.clear();
		calibrationStatus = "Hold the stick against the outer edge and rotate it several times.";
	}

	void FinalizeCenterCalibration() {
		const qss::CenterCalibrationResult result = qss::CalibrateCenter(calibrationSamples);
		if (calibrationHand == qss::ControllerHand::Left) {
			leftCenterResult = result;
		} else if (calibrationHand == qss::ControllerHand::Right) {
			rightCenterResult = result;
		}
		calibrationMode = CalibrationMode::None;
		calibrationStatus = result.valid ? "Center measurement completed." : "Center measurement failed: not enough samples.";
	}

	void FinalizeOuterCalibration() {
		qss::Vec2 center{};
		if (calibrationHand == qss::ControllerHand::Left) {
			center = leftCenterResult.valid
				? leftCenterResult.center
				: qss::Vec2{leftCorrection.centerX, leftCorrection.centerY};
			leftOuterResult = qss::CalibrateOuterRange(calibrationSamples, center);
			calibrationStatus = leftOuterResult.valid
				? "Outer-range measurement completed."
				: "Outer-range coverage is insufficient.";
		} else if (calibrationHand == qss::ControllerHand::Right) {
			center = rightCenterResult.valid
				? rightCenterResult.center
				: qss::Vec2{rightCorrection.centerX, rightCorrection.centerY};
			rightOuterResult = qss::CalibrateOuterRange(calibrationSamples, center);
			calibrationStatus = rightOuterResult.valid
				? "Outer-range measurement completed."
				: "Outer-range coverage is insufficient.";
		}
		calibrationMode = CalibrationMode::None;
	}

	void CaptureCalibrationSample(const qss::SharedScalarSample& sample) {
		if (calibrationMode == CalibrationMode::None ||
			sample.componentIndex >= components.size()) {
			return;
		}
		const qss::ScalarComponentSnapshot& component = components[sample.componentIndex];
		if (component.hand != calibrationHand ||
			(component.semantic != qss::ScalarSemantic::JoystickX &&
			 component.semantic != qss::ScalarSemantic::JoystickY)) {
			return;
		}

		const qss::LiveStickState& stick = calibrationHand == qss::ControllerHand::Left
			? live.GetLeft()
			: live.GetRight();
		if (!stick.x.available || !stick.y.available) {
			return;
		}
		if (calibrationSamples.size() < 500000) {
			calibrationSamples.push_back({stick.x.rawValue, stick.y.rawValue});
		}

		if (calibrationMode == CalibrationMode::Center) {
			const std::int64_t elapsed = qss::MonotonicClock::NowTicks() - calibrationStartTicks;
			if (elapsed >= qss::MonotonicClock::Frequency() * 3) {
				FinalizeCenterCalibration();
			}
		}
	}

	void ApplyCalibration(qss::ControllerHand hand) {
		qss::SharedHandCorrection* destination = hand == qss::ControllerHand::Left
			? &leftCorrection
			: &rightCorrection;
		const qss::CenterCalibrationResult& center = hand == qss::ControllerHand::Left
			? leftCenterResult
			: rightCenterResult;
		const qss::OuterCalibrationResult& outer = hand == qss::ControllerHand::Left
			? leftOuterResult
			: rightOuterResult;

		qss::CorrectionSettings settings = ToCorrectionSettings(*destination);
		if (center.valid) {
			settings.center = center.center;
			settings.innerDeadzone = center.recommendedDeadzone;
		}
		if (outer.valid) {
			settings.outerRadius = outer.radius;
		}
		const bool wasEnabled = destination->enabled != 0;
		*destination = ToSharedCorrection(settings);
		destination->enabled = wasEnabled ? 1U : 0U;
		MarkCorrectionDirty();
		calibrationStatus = "Calibration values copied to correction settings.";
	}

	void AppendLiveHistory(const qss::SharedScalarSample& sample) {
		if (sample.componentIndex >= components.size()) {
			return;
		}
		const qss::ScalarComponentSnapshot& component = components[sample.componentIndex];
		if (component.semantic != qss::ScalarSemantic::JoystickY) {
			return;
		}

		const qss::LiveStickState* stick = nullptr;
		std::deque<qss::StickPointSample>* history = nullptr;
		if (component.hand == qss::ControllerHand::Left) {
			stick = &live.GetLeft();
			history = &leftHistory;
		} else if (component.hand == qss::ControllerHand::Right) {
			stick = &live.GetRight();
			history = &rightHistory;
		}
		if (stick == nullptr || history == nullptr ||
			!stick->x.available || !stick->y.available) {
			return;
		}

		history->push_back({
			sample.timestampTicks,
			{stick->x.rawValue, stick->y.rawValue},
			{stick->x.outputValue, stick->y.outputValue},
		});
		const std::int64_t windowTicks = qss::MonotonicClock::Frequency() * 10;
		while (!history->empty() &&
			sample.timestampTicks - history->front().timestampTicks > windowTicks) {
			history->pop_front();
		}
	}

	void CaptureComponents(qss::RecordingData& recording) {
		const std::uint32_t count = reader.GetComponentCount();
		recording.components.resize(count);
		for (std::uint32_t index = 0; index < count; ++index) {
			qss::ScalarComponentSnapshot component;
			if (reader.ReadComponent(index, component)) {
				recording.components[index] = component;
			}
		}
	}

	void RefreshRecordingFiles() {
		recordingFiles.clear();
		const std::filesystem::path directory = GetRecordingDirectory();
		std::error_code error;
		if (directory.empty() || !std::filesystem::exists(directory, error)) {
			selectedRecordingIndex = -1;
			return;
		}
		for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
			if (error) {
				break;
			}
			if (entry.is_regular_file() && entry.path().extension() == L".qssrec") {
				recordingFiles.push_back(entry.path());
			}
		}
		std::sort(recordingFiles.begin(), recordingFiles.end(), std::greater<>());
		if (selectedRecordingIndex >= static_cast<int>(recordingFiles.size())) {
			selectedRecordingIndex = -1;
		}
	}

	bool StartRecording() {
		const bool windowsAvailable =
			windowsMouseObserver.IsRunning() || windowsRawInputObserver.IsRunning();
		if ((!connected && !windowsAvailable) || recordingActive) {
			recordingStatus = "No observable input source is available.";
			return false;
		}
		activeRecording = {};
		activeRecording.qpcFrequency = static_cast<std::uint64_t>(qss::MonotonicClock::Frequency());
		activeRecording.sessionId = connected ? sessionId : 0;
		activeRecording.probeState = connected ? reader.GetProbeState() : qss::ProbeState::Offline;
		activeRecording.leftCorrection = correctionLoaded
			? leftCorrection
			: qss::MakeDefaultSharedCorrection();
		activeRecording.rightCorrection = correctionLoaded
			? rightCorrection
			: qss::MakeDefaultSharedCorrection();
		if (connected) {
			const qss::LiveStickState& left = live.GetLeft();
			const qss::LiveStickState& right = live.GetRight();
			activeRecording.initialLeftX = left.x.rawValue;
			activeRecording.initialLeftY = left.y.rawValue;
			activeRecording.initialRightX = right.x.rawValue;
			activeRecording.initialRightY = right.y.rawValue;
			CaptureComponents(activeRecording);
		}
		recordingActive = true;
		recordingStatus = "Recording...";
		return true;
	}

	bool StopRecording() {
		if (!recordingActive) {
			return false;
		}
		recordingActive = false;
		const std::filesystem::path path = MakeRecordingFilePath();
		if (path.empty()) {
			recordingStatus = "Failed to resolve recording directory.";
			return false;
		}

		std::string error;
		if (!qss::SaveRecording(path, activeRecording, &error)) {
			recordingStatus = "Save failed: " + error;
			return false;
		}
		recordingStatus = "Saved: " + PathToUtf8(path.filename());
		RefreshRecordingFiles();
		return true;
	}

	void RebuildReplay(std::size_t cursor) {
		replayLive.Reset();
		for (std::size_t index = 0; index < replayRecording.components.size(); ++index) {
			replayLive.ConfigureComponent(
				static_cast<std::uint32_t>(index),
				replayRecording.components[index]
			);
		}
		replayCursor = std::min(cursor, replayRecording.samples.size());
		for (std::size_t index = 0; index < replayCursor; ++index) {
			replayLive.ConsumeSample(replayRecording.samples[index]);
		}
	}

	bool LoadReplay(const std::filesystem::path& path) {
		qss::RecordingData loaded;
		std::string error;
		if (!qss::LoadRecording(path, loaded, &error)) {
			replayLoaded = false;
			replayStatus = "Load failed: " + error;
			return false;
		}
		replayRecording = std::move(loaded);
		replayLoaded = true;
		replayStatus = "Loaded: " + PathToUtf8(path.filename());
		RebuildReplay(replayRecording.samples.size());
		windowsReplayCursor = replayRecording.windowsInputSamples.size();
		return true;
	}

	void ApplyCurrentCorrectionToReplay() {
		if (!replayLoaded || !correctionLoaded) {
			return;
		}
		qss::ReplayCorrectionSettings settings;
		settings.left = ToCorrectionSettings(leftCorrection);
		settings.right = ToCorrectionSettings(rightCorrection);
		qss::RecalculateRecordingOutputs(replayRecording, settings);
		RebuildReplay(replayCursor);
		replayStatus = "Replay outputs recalculated with current correction.";
	}

	void ResetSession() {
		live.Reset();
		nextSequence = 0;
		configuredComponentCount = 0;
		components.clear();
		latestComponentSamples.clear();
		componentHasSample.clear();
		leftHistory.clear();
		rightHistory.clear();
		correctionLoaded = false;
		correctionDirty = false;
	}

	void MarkCorrectionDirty() {
		correctionDirty = true;
		persistentCorrectionDirty = true;
		persistentCorrectionChangedTicks = qss::MonotonicClock::NowTicks();
	}

	void UpdateWindowsInput() {
		windowsXInputObserver.Poll();
		std::array<qss::WindowsMouseEvent, 256> events{};
		const std::size_t count = windowsMouseObserver.ReadEvents(events.data(), events.size());
		for (std::size_t index = 0; index < count; ++index) {
			windowsMouseEvents.push_back(events[index]);
			if (recordingActive) {
				activeRecording.windowsInputSamples.push_back(
					ToRecordingSample(events[index])
				);
			}
		}
		while (windowsMouseEvents.size() > 300) {
			windowsMouseEvents.pop_front();
		}

		std::array<qss::WindowsRawMouseEvent, 256> rawEvents{};
		const std::size_t rawCount = windowsRawInputObserver.ReadEvents(
			rawEvents.data(),
			rawEvents.size()
		);
		for (std::size_t index = 0; index < rawCount; ++index) {
			windowsRawMouseEvents.push_back(rawEvents[index]);
			if (recordingActive) {
				activeRecording.windowsInputSamples.push_back(
					ToRecordingSample(rawEvents[index])
				);
			}
		}
		while (windowsRawMouseEvents.size() > 300) {
			windowsRawMouseEvents.pop_front();
		}
	}

	void Update() {
		UpdateWindowsInput();
		if (!reader.IsOpen()) {
			connected = reader.Open();
			if (!connected) {
				return;
			}
			sessionId = reader.GetSessionId();
			ResetSession();
		}

		connected = true;
		if (!controller.IsOpen()) {
			controller.Open();
		}
		if (controller.IsOpen()) {
			controller.SetClientHeartbeat(qss::MonotonicClock::NowTicks());
		}

		const std::uint64_t currentSessionId = reader.GetSessionId();
		if (currentSessionId != sessionId) {
			if (recordingActive) {
				StopRecording();
			}
			sessionId = currentSessionId;
			ResetSession();
		}

		if (controller.IsOpen() && !correctionLoaded) {
			if (persistentCorrectionAvailable) {
				correctionLoaded = true;
				correctionDirty = true;
			} else {
				qss::CorrectionControlSnapshot control;
				if (controller.ReadCorrectionControl(control)) {
					leftCorrection = control.left;
					rightCorrection = control.right;
					correctionLoaded = true;
				}
			}
		}
		if (controller.IsOpen() && correctionLoaded && correctionDirty) {
			if (controller.WriteCorrectionControl(leftCorrection, rightCorrection)) {
				correctionDirty = false;
			}
		}
		if (persistentCorrectionDirty) {
			const std::int64_t now = qss::MonotonicClock::NowTicks();
			if (now - persistentCorrectionChangedTicks >= qss::MonotonicClock::Frequency() / 2) {
				SavePersistentCorrection();
			}
		}

		const std::uint32_t componentCount = reader.GetComponentCount();
		components.resize(componentCount);
		latestComponentSamples.resize(componentCount);
		componentHasSample.resize(componentCount, false);
		while (configuredComponentCount < componentCount) {
			qss::ScalarComponentSnapshot component;
			if (!reader.ReadComponent(configuredComponentCount, component)) {
				break;
			}
			live.ConfigureComponent(configuredComponentCount, component);
			components[configuredComponentCount] = component;
			++configuredComponentCount;
		}
		if (recordingActive && activeRecording.components.size() != componentCount) {
			CaptureComponents(activeRecording);
		}

		std::array<qss::SharedScalarSample, 256> samples{};
		for (;;) {
			const std::size_t count = reader.ReadSamples(nextSequence, samples.data(), samples.size());
			if (count == 0) {
				break;
			}
			for (std::size_t index = 0; index < count; ++index) {
				if (samples[index].componentIndex < latestComponentSamples.size()) {
					latestComponentSamples[samples[index].componentIndex] = samples[index];
					componentHasSample[samples[index].componentIndex] = true;
				}
				live.ConsumeSample(samples[index]);
				AppendLiveHistory(samples[index]);
				CaptureCalibrationSample(samples[index]);
				if (recordingActive && samples[index].componentIndex < activeRecording.components.size()) {
					activeRecording.samples.push_back(samples[index]);
				}
			}
		}
	}
};

const char* ProbeStateName(qss::ProbeState state) {
	switch (state) {
	case qss::ProbeState::Offline:
		return "Offline";
	case qss::ProbeState::PassThrough:
		return "Pass-through";
	case qss::ProbeState::Observing:
		return "Observing";
	case qss::ProbeState::Error:
		return "Error";
	}
	return "Unknown";
}

const char* HandName(qss::ControllerHand hand) {
	switch (hand) {
	case qss::ControllerHand::Left:
		return "Left";
	case qss::ControllerHand::Right:
		return "Right";
	case qss::ControllerHand::Unknown:
		return "Unknown";
	}
	return "Unknown";
}

const char* SemanticName(qss::ScalarSemantic semantic) {
	switch (semantic) {
	case qss::ScalarSemantic::JoystickX:
		return "Joystick X";
	case qss::ScalarSemantic::JoystickY:
		return "Joystick Y";
	case qss::ScalarSemantic::Unknown:
		return "Unknown";
	}
	return "Unknown";
}

void DrawStickPlot(
	const char* label,
	const qss::LiveStickState& stick,
	const std::deque<qss::StickPointSample>* history = nullptr
) {
	const double x = stick.x.rawValue;
	const double y = stick.y.rawValue;
	if (ImPlot::BeginPlot(label, ImVec2(-1.0F, 260.0F), ImPlotFlags_Equal)) {
		ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
		ImPlot::SetupAxesLimits(-1.1, 1.1, -1.1, 1.1, ImGuiCond_Always);

		const double circleX[] = {-1.0, -0.7071, 0.0, 0.7071, 1.0, 0.7071, 0.0, -0.7071, -1.0};
		const double circleY[] = {0.0, 0.7071, 1.0, 0.7071, 0.0, -0.7071, -1.0, -0.7071, 0.0};
		ImPlot::PlotLine("Unit circle", circleX, circleY, 9);
		if (history != nullptr && history->size() >= 2) {
			const std::size_t trailCount = std::min<std::size_t>(history->size(), 500);
			std::vector<double> trailX(trailCount);
			std::vector<double> trailY(trailCount);
			const std::size_t first = history->size() - trailCount;
			for (std::size_t index = 0; index < trailCount; ++index) {
				trailX[index] = (*history)[first + index].raw.x;
				trailY[index] = (*history)[first + index].raw.y;
			}
			ImPlot::PlotLine("Raw trail", trailX.data(), trailY.data(), static_cast<int>(trailCount));
		}
		if (stick.x.available && stick.y.available) {
			ImPlot::PlotScatter("Current", &x, &y, 1);
		}
		ImPlot::EndPlot();
	}

	ImGui::Text("Raw: X %+.5f   Y %+.5f", stick.x.rawValue, stick.y.rawValue);
	ImGui::Text("Output: X %+.5f   Y %+.5f", stick.x.outputValue, stick.y.outputValue);
	if (!stick.x.available || !stick.y.available) {
		ImGui::TextDisabled("Waiting for both stick axes.");
	}
}

void DrawHistoryStatistics(
	const char* plotId,
	const std::deque<qss::StickPointSample>& history
) {
	if (history.empty()) {
		ImGui::TextDisabled("No time-series samples yet.");
		return;
	}

	const std::int64_t latestTicks = history.back().timestampTicks;
	const double frequency = static_cast<double>(qss::MonotonicClock::Frequency());
	std::vector<double> times(history.size());
	std::vector<double> rawX(history.size());
	std::vector<double> rawY(history.size());
	std::vector<double> outputX(history.size());
	std::vector<double> outputY(history.size());
	std::vector<qss::StickPointSample> statisticsInput;
	statisticsInput.reserve(history.size());

	for (std::size_t index = 0; index < history.size(); ++index) {
		const qss::StickPointSample& sample = history[index];
		times[index] = static_cast<double>(sample.timestampTicks - latestTicks) / frequency;
		rawX[index] = sample.raw.x;
		rawY[index] = sample.raw.y;
		outputX[index] = sample.output.x;
		outputY[index] = sample.output.y;
		statisticsInput.push_back(sample);
	}

	if (ImPlot::BeginPlot(plotId, ImVec2(-1.0F, 220.0F))) {
		ImPlot::SetupAxes("Seconds", "Value");
		ImPlot::SetupAxisLimits(ImAxis_X1, -10.0, 0.0, ImGuiCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -1.1, 1.1, ImGuiCond_Always);
		ImPlot::PlotLine("Raw X", times.data(), rawX.data(), static_cast<int>(times.size()));
		ImPlot::PlotLine("Raw Y", times.data(), rawY.data(), static_cast<int>(times.size()));
		ImPlot::PlotLine("Output X", times.data(), outputX.data(), static_cast<int>(times.size()));
		ImPlot::PlotLine("Output Y", times.data(), outputY.data(), static_cast<int>(times.size()));
		ImPlot::EndPlot();
	}

	const qss::StickStatistics statistics = qss::CalculateStickStatistics(
		statisticsInput,
		qss::MonotonicClock::Frequency()
	);
	if (!statistics.valid) {
		return;
	}
	ImGui::Text(
		"Samples %zu | %.1f Hz | Mean (%+.4f, %+.4f) | StdDev (%.4f, %.4f)",
		statistics.sampleCount,
		statistics.updateHz,
		statistics.rawMean.x,
		statistics.rawMean.y,
		statistics.rawStandardDeviation.x,
		statistics.rawStandardDeviation.y
	);
	ImGui::Text(
		"Min (%+.4f, %+.4f) | Max (%+.4f, %+.4f) | Mean radius %.4f | Max radius %.4f",
		statistics.rawMinimum.x,
		statistics.rawMinimum.y,
		statistics.rawMaximum.x,
		statistics.rawMaximum.y,
		statistics.rawMeanRadius,
		statistics.rawMaximumRadius
	);
}

void DrawPipeline(const GuiState& state) {
	ImGui::SeparatorText("Pipeline");
	ImGui::BeginChild("Pipeline", ImVec2(0.0F, 92.0F), ImGuiChildFlags_Borders);
	ImGui::TextDisabled("Quest / Virtual Desktop (Quest side)");
	ImGui::SameLine();
	ImGui::TextUnformatted(" -> ");
	ImGui::SameLine();
	ImGui::Text("SteamVR S0: %s", state.connected ? ProbeStateName(state.reader.GetProbeState()) : "Offline");
	ImGui::Text(
		"Windows W0: LL Hook %s / Raw Input %s / XInput polling",
		state.windowsMouseObserver.IsRunning() ? "active" : "unavailable",
		state.windowsRawInputObserver.IsRunning() ? "active" : "unavailable"
	);
	ImGui::TextDisabled("Unobserved layers remain explicitly unobserved. No inferred values are shown.");
	ImGui::EndChild();
}

bool DrawCorrectionControls(qss::SharedHandCorrection& correction) {
	bool changed = false;
	bool enabled = correction.enabled != 0;
	if (ImGui::Checkbox("Correction enabled", &enabled)) {
		correction.enabled = enabled ? 1U : 0U;
		changed = true;
	}

	bool centerEnabled = correction.centerOffsetEnabled != 0;
	if (ImGui::Checkbox("Center offset", &centerEnabled)) {
		correction.centerOffsetEnabled = centerEnabled ? 1U : 0U;
		changed = true;
	}
	float center[2] = {correction.centerX, correction.centerY};
	if (ImGui::DragFloat2("Center X/Y", center, 0.001F, -0.5F, 0.5F, "%.4f")) {
		correction.centerX = center[0];
		correction.centerY = center[1];
		changed = true;
	}

	bool deadzoneEnabled = correction.innerDeadzoneEnabled != 0;
	if (ImGui::Checkbox("Inner deadzone", &deadzoneEnabled)) {
		correction.innerDeadzoneEnabled = deadzoneEnabled ? 1U : 0U;
		changed = true;
	}
	if (ImGui::SliderFloat("Deadzone radius", &correction.innerDeadzone, 0.0F, 0.5F, "%.4f")) {
		changed = true;
	}

	bool outerEnabled = correction.outerNormalizationEnabled != 0;
	if (ImGui::Checkbox("Outer normalization", &outerEnabled)) {
		correction.outerNormalizationEnabled = outerEnabled ? 1U : 0U;
		changed = true;
	}
	ImGui::TextDisabled("Outer table is currently initialized to 1.0; calibration will populate it.");
	return changed;
}

void DrawLiveView(GuiState& state) {
	DrawPipeline(state);
	ImGui::Spacing();

	if (!state.connected) {
		ImGui::TextDisabled("SteamVR Probe shared memory is not available.");
		return;
	}

	const qss::LiveStickState& left = state.live.GetLeft();
	const qss::LiveStickState& right = state.live.GetRight();

	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const float columnWidth = std::max(320.0F, (availableWidth - 12.0F) * 0.5F);
	ImGui::BeginChild("LeftStick", ImVec2(columnWidth, 500.0F), ImGuiChildFlags_Borders);
	ImGui::SeparatorText("Left Stick");
	DrawStickPlot("##LeftXY", left, &state.leftHistory);
	ImGui::PushID("LeftCorrection");
	if (state.correctionLoaded && DrawCorrectionControls(state.leftCorrection)) {
		state.MarkCorrectionDirty();
	}
	ImGui::PopID();
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("RightStick", ImVec2(0.0F, 500.0F), ImGuiChildFlags_Borders);
	ImGui::SeparatorText("Right Stick");
	DrawStickPlot("##RightXY", right, &state.rightHistory);
	ImGui::PushID("RightCorrection");
	if (state.correctionLoaded && DrawCorrectionControls(state.rightCorrection)) {
		state.MarkCorrectionDirty();
	}
	ImGui::PopID();
	ImGui::EndChild();

	ImGui::Spacing();
	ImGui::SeparatorText("Time / Statistics");
	ImGui::PushID("LeftHistory");
	ImGui::TextUnformatted("Left Stick");
	DrawHistoryStatistics("##LeftTime", state.leftHistory);
	ImGui::PopID();
	ImGui::PushID("RightHistory");
	ImGui::TextUnformatted("Right Stick");
	DrawHistoryStatistics("##RightTime", state.rightHistory);
	ImGui::PopID();
}

void DrawCalibrationHand(
	GuiState& state,
	qss::ControllerHand hand,
	const char* title,
	const qss::CenterCalibrationResult& center,
	const qss::OuterCalibrationResult& outer
) {
	ImGui::SeparatorText(title);
	const bool busy = state.calibrationMode != CalibrationMode::None;
	if (!busy) {
		if (ImGui::Button("Measure center")) {
			state.StartCenterCalibration(hand);
		}
		ImGui::SameLine();
		if (ImGui::Button("Measure outer range")) {
			state.StartOuterCalibration(hand);
		}
	}

	if (center.valid) {
		ImGui::Text(
			"Center: X %+.5f  Y %+.5f  Noise P99 %.5f  Deadzone %.5f",
			center.center.x,
			center.center.y,
			center.noiseRadiusP99,
			center.recommendedDeadzone
		);
	} else {
		ImGui::TextDisabled("Center: not measured");
	}

	ImGui::Text(
		"Outer directions: %zu / %zu%s",
		outer.measuredDirectionCount,
		qss::kOuterDirectionCount,
		outer.valid ? "" : " (insufficient)"
	);

	if (center.valid || outer.valid) {
		if (ImGui::Button("Apply measured values")) {
			state.ApplyCalibration(hand);
		}
	}
}

void DrawCalibrationView(GuiState& state) {
	if (!state.connected) {
		ImGui::TextDisabled("SteamVR Probe is offline.");
		return;
	}
	if (state.calibrationMode != CalibrationMode::None) {
		const char* hand = state.calibrationHand == qss::ControllerHand::Left ? "Left" : "Right";
		const char* mode = state.calibrationMode == CalibrationMode::Center ? "Center" : "Outer range";
		ImGui::Text("%s / %s measurement in progress", hand, mode);
		ImGui::Text("Captured samples: %zu", state.calibrationSamples.size());
		ImGui::TextWrapped("%s", state.calibrationStatus.c_str());
		if (state.calibrationMode == CalibrationMode::Outer && ImGui::Button("Finish outer measurement")) {
			state.FinalizeOuterCalibration();
		}
		if (ImGui::Button("Cancel measurement")) {
			state.calibrationMode = CalibrationMode::None;
			state.calibrationSamples.clear();
			state.calibrationStatus = "Measurement cancelled.";
		}
		ImGui::Separator();
	}

	ImGui::PushID("CalibrationLeft");
	DrawCalibrationHand(
		state,
		qss::ControllerHand::Left,
		"Left Stick",
		state.leftCenterResult,
		state.leftOuterResult
	);
	ImGui::PopID();

	ImGui::Spacing();
	ImGui::PushID("CalibrationRight");
	DrawCalibrationHand(
		state,
		qss::ControllerHand::Right,
		"Right Stick",
		state.rightCenterResult,
		state.rightOuterResult
	);
	ImGui::PopID();

	if (!state.calibrationStatus.empty() && state.calibrationMode == CalibrationMode::None) {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", state.calibrationStatus.c_str());
	}
}

const char* WindowsInputSourceName(qss::WindowsInputSource source) {
	switch (source) {
	case qss::WindowsInputSource::LowLevelMouse:
		return "LL Hook";
	case qss::WindowsInputSource::RawInputMouse:
		return "Raw Input";
	}
	return "Unknown";
}

const char* WindowsInputKindName(qss::WindowsInputKind kind) {
	switch (kind) {
	case qss::WindowsInputKind::Move:
		return "Move";
	case qss::WindowsInputKind::Wheel:
		return "Wheel";
	case qss::WindowsInputKind::HorizontalWheel:
		return "HWheel";
	case qss::WindowsInputKind::Button:
		return "Button";
	case qss::WindowsInputKind::Unknown:
		return "Unknown";
	}
	return "Unknown";
}

void DrawReplayWindowsInput(GuiState& state) {
	if (state.replayRecording.windowsInputSamples.empty()) {
		return;
	}

	ImGui::SeparatorText("Replay Windows W0");
	const qss::WindowsInputStatistics statistics = qss::AnalyzeWindowsInput(
		state.replayRecording.windowsInputSamples,
		static_cast<std::int64_t>(state.replayRecording.qpcFrequency)
	);
	ImGui::Text(
		"Duration %.3f s | LL %zu (injected %zu, wheel %zu) | Raw %zu (wheel %zu)",
		statistics.durationSeconds,
		statistics.lowLevelMouse.total,
		statistics.lowLevelInjected,
		statistics.lowLevelMouse.wheel + statistics.lowLevelMouse.horizontalWheel,
		statistics.rawInputMouse.total,
		statistics.rawInputMouse.wheel + statistics.rawInputMouse.horizontalWheel
	);
	ImGui::Text(
		"Wheel delta sum: LL %lld / Raw %lld",
		static_cast<long long>(statistics.lowLevelMouse.wheelDeltaSum),
		static_cast<long long>(statistics.rawInputMouse.wheelDeltaSum)
	);
	int cursor = static_cast<int>(std::min<std::size_t>(
		state.windowsReplayCursor,
		static_cast<std::size_t>(INT_MAX)
	));
	const int maxCursor = static_cast<int>(std::min<std::size_t>(
		state.replayRecording.windowsInputSamples.size(),
		static_cast<std::size_t>(INT_MAX)
	));
	if (ImGui::SliderInt("Windows sample position", &cursor, 0, maxCursor)) {
		state.windowsReplayCursor = static_cast<std::size_t>(cursor);
	}

	const std::size_t center = std::min(
		state.windowsReplayCursor,
		state.replayRecording.windowsInputSamples.size()
	);
	const std::size_t first = center > 50 ? center - 50 : 0;
	const std::size_t last = std::min(
		state.replayRecording.windowsInputSamples.size(),
		center + 50
	);
	const std::int64_t originTicks =
		state.replayRecording.windowsInputSamples.front().timestampTicks;
	const double frequency = state.replayRecording.qpcFrequency > 0
		? static_cast<double>(state.replayRecording.qpcFrequency)
		: 1.0;

	if (ImGui::BeginTable(
		"ReplayWindowsEvents",
		10,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
		ImVec2(0.0F, 280.0F)
	)) {
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 55.0F);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 85.0F);
		ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 75.0F);
		ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 65.0F);
		ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 65.0F);
		ImGui::TableSetupColumn("dX", ImGuiTableColumnFlags_WidthFixed, 65.0F);
		ImGui::TableSetupColumn("dY", ImGuiTableColumnFlags_WidthFixed, 65.0F);
		ImGui::TableSetupColumn("Wheel", ImGuiTableColumnFlags_WidthFixed, 70.0F);
		ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 90.0F);
		ImGui::TableHeadersRow();

		for (std::size_t index = first; index < last; ++index) {
			const qss::WindowsInputSample& sample =
				state.replayRecording.windowsInputSamples[index];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%zu", index);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text(
				"%.4f",
				static_cast<double>(sample.timestampTicks - originTicks) / frequency
			);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(WindowsInputSourceName(sample.source));
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(WindowsInputKindName(sample.kind));
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%d", sample.x);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d", sample.y);
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%d", sample.deltaX);
			ImGui::TableSetColumnIndex(7);
			ImGui::Text("%d", sample.deltaY);
			ImGui::TableSetColumnIndex(8);
			ImGui::Text("%d", sample.wheelDelta);
			ImGui::TableSetColumnIndex(9);
			ImGui::Text("0x%08X", sample.flags);
		}
		ImGui::EndTable();
	}
}

void DrawRecordingView(GuiState& state) {
	ImGui::SeparatorText("Record");
	if (state.recordingActive) {
		if (ImGui::Button("Stop recording")) {
			state.StopRecording();
		}
		ImGui::SameLine();
		ImGui::Text(
			"SteamVR samples: %zu   Windows samples: %zu",
			state.activeRecording.samples.size(),
			state.activeRecording.windowsInputSamples.size()
		);
	} else {
		const bool canRecord = state.connected ||
			state.windowsMouseObserver.IsRunning() ||
			state.windowsRawInputObserver.IsRunning();
		if (!canRecord) {
			ImGui::TextDisabled("No observable input source is available.");
		} else if (ImGui::Button("Start recording")) {
			state.StartRecording();
		}
	}
	if (!state.recordingStatus.empty()) {
		ImGui::TextWrapped("%s", state.recordingStatus.c_str());
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Saved recordings");
	if (ImGui::Button("Refresh")) {
		state.RefreshRecordingFiles();
	}
	if (state.recordingFiles.empty()) {
		state.RefreshRecordingFiles();
	}
	ImGui::BeginChild("RecordingFiles", ImVec2(0.0F, 150.0F), ImGuiChildFlags_Borders);
	for (int index = 0; index < static_cast<int>(state.recordingFiles.size()); ++index) {
		const std::string label = PathToUtf8(state.recordingFiles[index].filename());
		if (ImGui::Selectable(label.c_str(), state.selectedRecordingIndex == index)) {
			state.selectedRecordingIndex = index;
		}
	}
	ImGui::EndChild();

	if (state.selectedRecordingIndex >= 0 &&
		state.selectedRecordingIndex < static_cast<int>(state.recordingFiles.size())) {
		if (ImGui::Button("Load selected recording")) {
			state.LoadReplay(state.recordingFiles[state.selectedRecordingIndex]);
		}
	}
	if (!state.replayStatus.empty()) {
		ImGui::TextWrapped("%s", state.replayStatus.c_str());
	}

	if (!state.replayLoaded) {
		return;
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Replay");
	ImGui::Text(
		"Components: %zu   SteamVR samples: %zu   Windows samples: %zu   Session: %llu",
		state.replayRecording.components.size(),
		state.replayRecording.samples.size(),
		state.replayRecording.windowsInputSamples.size(),
		static_cast<unsigned long long>(state.replayRecording.sessionId)
	);

	const std::size_t maxCursorSize = state.replayRecording.samples.size();
	int cursor = static_cast<int>(std::min<std::size_t>(
		state.replayCursor,
		static_cast<std::size_t>(INT_MAX)
	));
	const int maxCursor = static_cast<int>(std::min<std::size_t>(
		maxCursorSize,
		static_cast<std::size_t>(INT_MAX)
	));
	if (ImGui::SliderInt("Sample position", &cursor, 0, maxCursor)) {
		state.RebuildReplay(static_cast<std::size_t>(cursor));
	}
	if (state.correctionLoaded && ImGui::Button("Reapply current correction")) {
		state.ApplyCurrentCorrectionToReplay();
	}

	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const float columnWidth = std::max(320.0F, (availableWidth - 12.0F) * 0.5F);
	ImGui::BeginChild("ReplayLeft", ImVec2(columnWidth, 330.0F), ImGuiChildFlags_Borders);
	ImGui::SeparatorText("Replay Left");
	DrawStickPlot("##ReplayLeftXY", state.replayLive.GetLeft());
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("ReplayRight", ImVec2(0.0F, 330.0F), ImGuiChildFlags_Borders);
	ImGui::SeparatorText("Replay Right");
	DrawStickPlot("##ReplayRightXY", state.replayLive.GetRight());
	ImGui::EndChild();

	DrawReplayWindowsInput(state);
}

const char* MouseEventTypeName(qss::WindowsMouseEventType type) {
	switch (type) {
	case qss::WindowsMouseEventType::Move:
		return "Move";
	case qss::WindowsMouseEventType::LeftDown:
		return "Left Down";
	case qss::WindowsMouseEventType::LeftUp:
		return "Left Up";
	case qss::WindowsMouseEventType::RightDown:
		return "Right Down";
	case qss::WindowsMouseEventType::RightUp:
		return "Right Up";
	case qss::WindowsMouseEventType::MiddleDown:
		return "Middle Down";
	case qss::WindowsMouseEventType::MiddleUp:
		return "Middle Up";
	case qss::WindowsMouseEventType::Wheel:
		return "Wheel";
	case qss::WindowsMouseEventType::HorizontalWheel:
		return "HWheel";
	case qss::WindowsMouseEventType::XButtonDown:
		return "X Down";
	case qss::WindowsMouseEventType::XButtonUp:
		return "X Up";
	case qss::WindowsMouseEventType::Unknown:
		return "Unknown";
	}
	return "Unknown";
}

void DrawWindowsInputDiagnostics(GuiState& state) {
	ImGui::SeparatorText("Windows W0 / Mouse");
	ImGui::Text(
		"Observer: %s   Buffered events: %zu   Dropped: %zu",
		state.windowsMouseObserver.IsRunning() ? "Active" : "Unavailable",
		state.windowsMouseEvents.size(),
		state.windowsMouseObserver.DroppedCount()
	);
	ImGui::TextDisabled("Injected indicates that Windows marked the event as software-injected.");

	if (ImGui::BeginTable(
		"WindowsMouseEvents",
		6,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
		ImVec2(0.0F, 220.0F)
	)) {
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0F);
		ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 75.0F);
		ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 75.0F);
		ImGui::TableSetupColumn("Wheel", ImGuiTableColumnFlags_WidthFixed, 70.0F);
		ImGui::TableSetupColumn("Injected", ImGuiTableColumnFlags_WidthFixed, 70.0F);
		ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 85.0F);
		ImGui::TableHeadersRow();

		const std::size_t first = state.windowsMouseEvents.size() > 100
			? state.windowsMouseEvents.size() - 100
			: 0;
		for (std::size_t index = first; index < state.windowsMouseEvents.size(); ++index) {
			const qss::WindowsMouseEvent& event = state.windowsMouseEvents[index];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(MouseEventTypeName(event.type));
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", event.x);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", event.y);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d", event.wheelDelta);
			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted((event.flags & LLMHF_INJECTED) != 0 ? "Yes" : "No");
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("0x%08X", event.flags);
		}
		ImGui::EndTable();
	}
}

void DrawRawInputDiagnostics(GuiState& state) {
	ImGui::SeparatorText("Windows W0 / Raw Input Mouse");
	ImGui::Text(
		"Observer: %s   Buffered events: %zu   Dropped: %zu",
		state.windowsRawInputObserver.IsRunning() ? "Active" : "Unavailable",
		state.windowsRawMouseEvents.size(),
		state.windowsRawInputObserver.DroppedCount()
	);
	ImGui::TextDisabled("Compare this with LL Hook events to determine how Virtual Desktop reaches Windows.");

	if (ImGui::BeginTable(
		"WindowsRawMouseEvents",
		7,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
		ImVec2(0.0F, 220.0F)
	)) {
		ImGui::TableSetupColumn("dX", ImGuiTableColumnFlags_WidthFixed, 65.0F);
		ImGui::TableSetupColumn("dY", ImGuiTableColumnFlags_WidthFixed, 65.0F);
		ImGui::TableSetupColumn("Wheel", ImGuiTableColumnFlags_WidthFixed, 70.0F);
		ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("Mouse flags", ImGuiTableColumnFlags_WidthFixed, 90.0F);
		ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed, 120.0F);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 120.0F);
		ImGui::TableHeadersRow();

		const std::size_t first = state.windowsRawMouseEvents.size() > 100
			? state.windowsRawMouseEvents.size() - 100
			: 0;
		for (std::size_t index = first; index < state.windowsRawMouseEvents.size(); ++index) {
			const qss::WindowsRawMouseEvent& event = state.windowsRawMouseEvents[index];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%d", event.deltaX);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", event.deltaY);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", event.wheelDelta);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("0x%04X", event.buttonFlags);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("0x%04X", event.mouseFlags);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("0x%llX", static_cast<unsigned long long>(event.device));
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%lld", static_cast<long long>(event.timestampTicks));
		}
		ImGui::EndTable();
	}
}

void DrawXInputDiagnostics(GuiState& state) {
	ImGui::SeparatorText("Windows W0 / XInput");
	ImGui::TextDisabled("Slots 0-3 are polled continuously to detect any Virtual Desktop gamepad exposure.");

	if (ImGui::BeginTable(
		"XInputSlots",
		8,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
	)) {
		ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 45.0F);
		ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("Packet", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("LX", ImGuiTableColumnFlags_WidthFixed, 85.0F);
		ImGui::TableSetupColumn("LY", ImGuiTableColumnFlags_WidthFixed, 85.0F);
		ImGui::TableSetupColumn("RX", ImGuiTableColumnFlags_WidthFixed, 85.0F);
		ImGui::TableSetupColumn("RY", ImGuiTableColumnFlags_WidthFixed, 85.0F);
		ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, 85.0F);
		ImGui::TableHeadersRow();

		const auto& slots = state.windowsXInputObserver.GetSlots();
		for (std::size_t index = 0; index < slots.size(); ++index) {
			const qss::XInputSlotState& slot = slots[index];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%zu", index);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(slot.connected ? "Connected" : "Offline");
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%u", slot.packetNumber);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%+.5f", slot.leftX);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%+.5f", slot.leftY);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%+.5f", slot.rightX);
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%+.5f", slot.rightY);
			ImGui::TableSetColumnIndex(7);
			ImGui::Text("0x%04X", slot.buttons);
		}
		ImGui::EndTable();
	}
}

void DrawProcessIdentity(
	const char* label,
	const qss::ProcessIdentity& identity
) {
	ImGui::SeparatorText(label);
	if (!identity.running) {
		ImGui::TextDisabled("Not running");
		if (!identity.error.empty()) {
			ImGui::TextDisabled("%s", identity.error.c_str());
		}
		return;
	}

	ImGui::Text("PID: %u", identity.processId);
	ImGui::TextWrapped("Path: %s", PathToUtf8(identity.executablePath).c_str());
	ImGui::Text("Version: %s", identity.fileVersion.empty() ? "<unavailable>" : identity.fileVersion.c_str());
	ImGui::TextWrapped("SHA-256: %s", identity.sha256.empty() ? "<unavailable>" : identity.sha256.c_str());
	if (!identity.error.empty()) {
		ImGui::TextDisabled("%s", identity.error.c_str());
	}
}

void DrawCompatibilityDiagnostics(GuiState& state) {
	ImGui::SeparatorText("Compatibility identity");
	if (!state.processIdentityScanned || ImGui::Button("Refresh process identity")) {
		state.RefreshProcessIdentities();
	}
	DrawProcessIdentity("SteamVR / vrserver.exe", state.steamVrIdentity);
	DrawProcessIdentity("Virtual Desktop Streamer", state.virtualDesktopIdentity);
}

void DrawDiagnosticsView(GuiState& state) {
	if (!state.connected) {
		ImGui::TextDisabled("SteamVR Probe: Offline");
		DrawWindowsInputDiagnostics(state);
		DrawRawInputDiagnostics(state);
		DrawXInputDiagnostics(state);
		DrawCompatibilityDiagnostics(state);
		return;
	}

	ImGui::Text("SteamVR Probe: %s", ProbeStateName(state.reader.GetProbeState()));
	ImGui::Text("Session: %llu", static_cast<unsigned long long>(state.sessionId));
	ImGui::Text("Components: %u", state.reader.GetComponentCount());
	ImGui::Text("Observed sequence: %llu", static_cast<unsigned long long>(state.live.GetLastSequence()));
	ImGui::Text("Detected sample gaps: %llu", static_cast<unsigned long long>(state.live.GetDroppedSampleCount()));
	if (!state.persistenceStatus.empty()) {
		ImGui::TextWrapped("Settings: %s", state.persistenceStatus.c_str());
	}
	ImGui::Separator();

	if (ImGui::BeginTable("Components", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0F, 260.0F))) {
		ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0F);
		ImGui::TableSetupColumn("Hand", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("Semantic", ImGuiTableColumnFlags_WidthFixed, 100.0F);
		ImGui::TableSetupColumn("Path");
		ImGui::TableSetupColumn("Raw", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("Sequence", ImGuiTableColumnFlags_WidthFixed, 90.0F);
		ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 110.0F);
		ImGui::TableSetupColumn("Container", ImGuiTableColumnFlags_WidthFixed, 110.0F);
		ImGui::TableHeadersRow();

		for (std::uint32_t index = 0; index < state.reader.GetComponentCount(); ++index) {
			qss::ScalarComponentSnapshot component;
			if (!state.reader.ReadComponent(index, component)) {
				continue;
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%u", index);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(HandName(component.hand));
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(SemanticName(component.semantic));
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(component.path.data());
			ImGui::TableSetColumnIndex(4);
			if (index < state.componentHasSample.size() && state.componentHasSample[index]) {
				const qss::SharedScalarSample& sample = state.latestComponentSamples[index];
				ImGui::Text("%+.5f", sample.rawValue);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%+.5f", sample.outputValue);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%llu", static_cast<unsigned long long>(sample.sequence));
			} else {
				ImGui::TextDisabled("-");
				ImGui::TableSetColumnIndex(5);
				ImGui::TextDisabled("-");
				ImGui::TableSetColumnIndex(6);
				ImGui::TextDisabled("-");
			}
			ImGui::TableSetColumnIndex(7);
			ImGui::Text("%llu", static_cast<unsigned long long>(component.handle));
			ImGui::TableSetColumnIndex(8);
			ImGui::Text("%llu", static_cast<unsigned long long>(component.container));
		}
		ImGui::EndTable();
	}

	DrawWindowsInputDiagnostics(state);
	DrawRawInputDiagnostics(state);
	DrawXInputDiagnostics(state);
	DrawCompatibilityDiagnostics(state);
}

void DrawPlaceholder(const char* title, const char* message) {
	ImGui::SeparatorText(title);
	ImGui::TextWrapped("%s", message);
}

void DrawMainWindow(GuiState& state) {
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::Begin(
		"QuestStickScope",
		nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
	);

	ImGui::TextUnformatted("QuestStickScope");
	ImGui::SameLine();
	ImGui::TextDisabled("SteamVR input observation");
	ImGui::Separator();

	if (ImGui::BeginTabBar("MainTabs")) {
		if (ImGui::BeginTabItem("Live")) {
			DrawLiveView(state);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Calibration")) {
			DrawCalibrationView(state);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Recording")) {
			DrawRecordingView(state);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Diagnostics")) {
			DrawDiagnosticsView(state);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

bool CreateRenderTarget() {
	ID3D11Texture2D* backBuffer = nullptr;
	if (FAILED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
		return false;
	}
	const HRESULT result = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
	backBuffer->Release();
	return SUCCEEDED(result);
}

void CleanupRenderTarget() {
	if (g_renderTarget != nullptr) {
		g_renderTarget->Release();
		g_renderTarget = nullptr;
	}
}

bool CreateDeviceD3D(HWND window) {
	DXGI_SWAP_CHAIN_DESC desc{};
	desc.BufferCount = 2;
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.OutputWindow = window;
	desc.SampleDesc.Count = 1;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	const D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
	D3D_FEATURE_LEVEL selectedFeatureLevel{};
	HRESULT result = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0,
		featureLevels,
		static_cast<UINT>(std::size(featureLevels)),
		D3D11_SDK_VERSION,
		&desc,
		&g_swapChain,
		&g_device,
		&selectedFeatureLevel,
		&g_deviceContext
	);
	if (result == DXGI_ERROR_UNSUPPORTED) {
		result = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			0,
			featureLevels,
			static_cast<UINT>(std::size(featureLevels)),
			D3D11_SDK_VERSION,
			&desc,
			&g_swapChain,
			&g_device,
			&selectedFeatureLevel,
			&g_deviceContext
		);
	}
	return SUCCEEDED(result) && CreateRenderTarget();
}

void CleanupDeviceD3D() {
	CleanupRenderTarget();
	if (g_swapChain != nullptr) {
		g_swapChain->Release();
		g_swapChain = nullptr;
	}
	if (g_deviceContext != nullptr) {
		g_deviceContext->Release();
		g_deviceContext = nullptr;
	}
	if (g_device != nullptr) {
		g_device->Release();
		g_device = nullptr;
	}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	qss::WindowsRawInputObserver::HandleWindowMessage(message, wParam, lParam);
	if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
		return TRUE;
	}

	switch (message) {
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED) {
			g_resizeWidth = static_cast<UINT>(LOWORD(lParam));
			g_resizeHeight = static_cast<UINT>(HIWORD(lParam));
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0U) == SC_KEYMENU) {
			return 0;
		}
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	default:
		break;
	}
	return ::DefWindowProcW(window, message, wParam, lParam);
}

int RunGui(HINSTANCE instance) {
	ImGui_ImplWin32_EnableDpiAwareness();
	const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
		::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY)
	);

	WNDCLASSEXW windowClass{
		sizeof(WNDCLASSEXW),
		CS_CLASSDC,
		WndProc,
		0L,
		0L,
		instance,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		L"QuestStickScopeWindow",
		nullptr,
	};
	::RegisterClassExW(&windowClass);

	HWND window = ::CreateWindowW(
		windowClass.lpszClassName,
		L"QuestStickScope",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		static_cast<int>(1280.0F * scale),
		static_cast<int>(820.0F * scale),
		nullptr,
		nullptr,
		instance,
		nullptr
	);
	if (window == nullptr || !CreateDeviceD3D(window)) {
		CleanupDeviceD3D();
		if (window != nullptr) {
			::DestroyWindow(window);
		}
		::UnregisterClassW(windowClass.lpszClassName, instance);
		return 1;
	}

	::ShowWindow(window, SW_SHOWDEFAULT);
	::UpdateWindow(window);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = scale;

	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(g_device, g_deviceContext);

	GuiState state(window);
	bool done = false;
	while (!done) {
		MSG message{};
		while (::PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
			::TranslateMessage(&message);
			::DispatchMessageW(&message);
			if (message.message == WM_QUIT) {
				done = true;
			}
		}
		if (done) {
			break;
		}

		state.Update();
		if (g_swapChainOccluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
			::Sleep(10);
			continue;
		}
		g_swapChainOccluded = false;

		if (g_resizeWidth != 0 && g_resizeHeight != 0) {
			CleanupRenderTarget();
			g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_resizeWidth = 0;
			g_resizeHeight = 0;
			CreateRenderTarget();
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		DrawMainWindow(state);

		ImGui::Render();
		const float clearColor[4] = {0.035F, 0.04F, 0.05F, 1.0F};
		g_deviceContext->OMSetRenderTargets(1, &g_renderTarget, nullptr);
		g_deviceContext->ClearRenderTargetView(g_renderTarget, clearColor);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		const HRESULT presentResult = g_swapChain->Present(1, 0);
		g_swapChainOccluded = (presentResult == DXGI_STATUS_OCCLUDED);
	}

	if (state.recordingActive) {
		state.StopRecording();
	}
	if (state.persistentCorrectionDirty) {
		state.SavePersistentCorrection();
	}
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();
	CleanupDeviceD3D();
	::DestroyWindow(window);
	::UnregisterClassW(windowClass.lpszClassName, instance);
	return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
	return RunGui(instance);
}

#endif
