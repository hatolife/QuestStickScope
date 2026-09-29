#ifdef _WIN32

#include "core/live/SteamVRLiveState.hpp"
#include "core/recording/Recording.hpp"
#include "core/recording/Replay.hpp"
#include "platform/Clock.hpp"
#include "platform/windows/SteamVRSharedMemory.hpp"

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

std::filesystem::path GetRecordingDirectory() {
	PWSTR localAppData = nullptr;
	if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) {
		return {};
	}
	const std::filesystem::path directory =
		std::filesystem::path(localAppData) / L"QuestStickScope" / L"recordings";
	::CoTaskMemFree(localAppData);
	return directory;
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

struct GuiState {
	qss::SteamVRSharedMemoryReader reader;
	qss::SteamVRSharedMemoryController controller;
	qss::SteamVRLiveState live;
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
	bool replayLoaded = false;
	std::string replayStatus;

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
		if (!connected || !correctionLoaded || recordingActive) {
			recordingStatus = "Waiting for SteamVR input and correction state.";
			return false;
		}
		activeRecording = {};
		activeRecording.qpcFrequency = static_cast<std::uint64_t>(qss::MonotonicClock::Frequency());
		activeRecording.sessionId = sessionId;
		activeRecording.probeState = reader.GetProbeState();
		activeRecording.leftCorrection = leftCorrection;
		activeRecording.rightCorrection = rightCorrection;
		const qss::LiveStickState& left = live.GetLeft();
		const qss::LiveStickState& right = live.GetRight();
		activeRecording.initialLeftX = left.x.rawValue;
		activeRecording.initialLeftY = left.y.rawValue;
		activeRecording.initialRightX = right.x.rawValue;
		activeRecording.initialRightY = right.y.rawValue;
		CaptureComponents(activeRecording);
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
		correctionLoaded = false;
		correctionDirty = false;
	}

	void MarkCorrectionDirty() {
		correctionDirty = true;
	}

	void Update() {
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
			qss::CorrectionControlSnapshot control;
			if (controller.ReadCorrectionControl(control)) {
				leftCorrection = control.left;
				rightCorrection = control.right;
				correctionLoaded = true;
			}
		}
		if (controller.IsOpen() && correctionLoaded && correctionDirty) {
			if (controller.WriteCorrectionControl(leftCorrection, rightCorrection)) {
				correctionDirty = false;
			}
		}

		const std::uint32_t componentCount = reader.GetComponentCount();
		while (configuredComponentCount < componentCount) {
			qss::ScalarComponentSnapshot component;
			if (!reader.ReadComponent(configuredComponentCount, component)) {
				break;
			}
			live.ConfigureComponent(configuredComponentCount, component);
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
				live.ConsumeSample(samples[index]);
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

void DrawStickPlot(const char* label, const qss::LiveStickState& stick) {
	const double x = stick.x.rawValue;
	const double y = stick.y.rawValue;
	if (ImPlot::BeginPlot(label, ImVec2(-1.0F, 260.0F), ImPlotFlags_Equal)) {
		ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
		ImPlot::SetupAxesLimits(-1.1, 1.1, -1.1, 1.1, ImGuiCond_Always);

		const double circleX[] = {-1.0, -0.7071, 0.0, 0.7071, 1.0, 0.7071, 0.0, -0.7071, -1.0};
		const double circleY[] = {0.0, 0.7071, 1.0, 0.7071, 0.0, -0.7071, -1.0, -0.7071, 0.0};
		ImPlot::PlotLine("Unit circle", circleX, circleY, 9);
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

void DrawPipeline(const GuiState& state) {
	ImGui::SeparatorText("Pipeline");
	ImGui::BeginChild("Pipeline", ImVec2(0.0F, 92.0F), ImGuiChildFlags_Borders);
	ImGui::TextDisabled("Quest / Virtual Desktop (Quest side)");
	ImGui::SameLine();
	ImGui::TextUnformatted(" -> ");
	ImGui::SameLine();
	ImGui::Text("SteamVR S0: %s", state.connected ? ProbeStateName(state.reader.GetProbeState()) : "Offline");
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
	ImGui::BeginChild("LeftStick", ImVec2(columnWidth, 0.0F), ImGuiChildFlags_Borders);
	ImGui::SeparatorText("Left Stick");
	DrawStickPlot("##LeftXY", left);
	ImGui::PushID("LeftCorrection");
	if (state.correctionLoaded && DrawCorrectionControls(state.leftCorrection)) {
		state.MarkCorrectionDirty();
	}
	ImGui::PopID();
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("RightStick", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders);
	ImGui::SeparatorText("Right Stick");
	DrawStickPlot("##RightXY", right);
	ImGui::PushID("RightCorrection");
	if (state.correctionLoaded && DrawCorrectionControls(state.rightCorrection)) {
		state.MarkCorrectionDirty();
	}
	ImGui::PopID();
	ImGui::EndChild();
}

void DrawRecordingView(GuiState& state) {
	ImGui::SeparatorText("Record");
	if (!state.connected) {
		ImGui::TextDisabled("SteamVR Probe is offline.");
	} else if (state.recordingActive) {
		if (ImGui::Button("Stop recording")) {
			state.StopRecording();
		}
		ImGui::SameLine();
		ImGui::Text("Samples: %zu", state.activeRecording.samples.size());
	} else {
		if (ImGui::Button("Start recording")) {
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
		"Components: %zu   Samples: %zu   Session: %llu",
		state.replayRecording.components.size(),
		state.replayRecording.samples.size(),
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
}

void DrawDiagnosticsView(GuiState& state) {
	if (!state.connected) {
		ImGui::TextDisabled("SteamVR Probe: Offline");
		return;
	}

	ImGui::Text("SteamVR Probe: %s", ProbeStateName(state.reader.GetProbeState()));
	ImGui::Text("Session: %llu", static_cast<unsigned long long>(state.sessionId));
	ImGui::Text("Components: %u", state.reader.GetComponentCount());
	ImGui::Text("Observed sequence: %llu", static_cast<unsigned long long>(state.live.GetLastSequence()));
	ImGui::Text("Detected sample gaps: %llu", static_cast<unsigned long long>(state.live.GetDroppedSampleCount()));
	ImGui::Separator();

	if (ImGui::BeginTable("Components", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
		ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 52.0F);
		ImGui::TableSetupColumn("Hand", ImGuiTableColumnFlags_WidthFixed, 80.0F);
		ImGui::TableSetupColumn("Semantic", ImGuiTableColumnFlags_WidthFixed, 100.0F);
		ImGui::TableSetupColumn("Path");
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
			ImGui::Text("%llu", static_cast<unsigned long long>(component.handle));
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%llu", static_cast<unsigned long long>(component.container));
		}
		ImGui::EndTable();
	}
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
			DrawPlaceholder("Calibration", "Center and outer-range calibration will be connected after live observation is verified on the target hardware.");
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

	GuiState state;
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
