#include "platform/windows/WindowsRawInputObserver.hpp"

#ifdef _WIN32

#include "platform/Clock.hpp"

namespace qss {

WindowsRawInputObserver* WindowsRawInputObserver::s_instance = nullptr;

WindowsRawInputObserver::~WindowsRawInputObserver() {
	Stop();
}

bool WindowsRawInputObserver::Start(HWND targetWindow) noexcept {
	if (m_targetWindow != nullptr) {
		return true;
	}
	if (targetWindow == nullptr || (s_instance != nullptr && s_instance != this)) {
		return false;
	}

	RAWINPUTDEVICE device{};
	device.usUsagePage = 0x01;
	device.usUsage = 0x02;
	device.dwFlags = RIDEV_INPUTSINK;
	device.hwndTarget = targetWindow;
	if (::RegisterRawInputDevices(&device, 1, sizeof(device)) == FALSE) {
		return false;
	}

	m_targetWindow = targetWindow;
	s_instance = this;
	return true;
}

void WindowsRawInputObserver::Stop() noexcept {
	if (m_targetWindow != nullptr) {
		RAWINPUTDEVICE device{};
		device.usUsagePage = 0x01;
		device.usUsage = 0x02;
		device.dwFlags = RIDEV_REMOVE;
		device.hwndTarget = nullptr;
		::RegisterRawInputDevices(&device, 1, sizeof(device));
		m_targetWindow = nullptr;
	}
	if (s_instance == this) {
		s_instance = nullptr;
	}
}

bool WindowsRawInputObserver::IsRunning() const noexcept {
	return m_targetWindow != nullptr;
}

std::size_t WindowsRawInputObserver::ReadEvents(
	WindowsRawMouseEvent* output,
	std::size_t capacity
) noexcept {
	if (output == nullptr) {
		return 0;
	}

	std::size_t count = 0;
	while (count < capacity && m_events.TryPop(output[count])) {
		++count;
	}
	return count;
}

std::size_t WindowsRawInputObserver::DroppedCount() const noexcept {
	return m_events.DroppedCount();
}

void WindowsRawInputObserver::HandleWindowMessage(
	UINT message,
	WPARAM wParam,
	LPARAM lParam
) noexcept {
	(void)wParam;
	if (message == WM_INPUT && s_instance != nullptr && lParam != 0) {
		s_instance->HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
	}
}

void WindowsRawInputObserver::HandleRawInput(HRAWINPUT rawInput) noexcept {
	RAWINPUT input{};
	UINT size = sizeof(input);
	const UINT result = ::GetRawInputData(
		rawInput,
		RID_INPUT,
		&input,
		&size,
		sizeof(RAWINPUTHEADER)
	);
	if (result == static_cast<UINT>(-1) ||
		input.header.dwType != RIM_TYPEMOUSE) {
		return;
	}

	const RAWMOUSE& mouse = input.data.mouse;
	WindowsRawMouseEvent event;
	event.timestampTicks = MonotonicClock::NowTicks();
	event.device = reinterpret_cast<std::uintptr_t>(input.header.hDevice);
	event.deltaX = mouse.lLastX;
	event.deltaY = mouse.lLastY;
	event.buttonFlags = mouse.usButtonFlags;
	event.rawButtonData = mouse.usButtonData;
	event.mouseFlags = mouse.usFlags;

	if ((mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0 ||
		(mouse.usButtonFlags & RI_MOUSE_HWHEEL) != 0) {
		event.wheelDelta = static_cast<std::int16_t>(mouse.usButtonData);
	}
	m_events.TryPush(event);
}

} // namespace qss

#endif
