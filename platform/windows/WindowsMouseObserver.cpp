#include "platform/windows/WindowsMouseObserver.hpp"

#ifdef _WIN32

#include "platform/Clock.hpp"

namespace qss {

WindowsMouseObserver* WindowsMouseObserver::s_instance = nullptr;

namespace {

WindowsMouseEventType ToEventType(WPARAM message) noexcept {
	switch (message) {
	case WM_MOUSEMOVE:
		return WindowsMouseEventType::Move;
	case WM_LBUTTONDOWN:
		return WindowsMouseEventType::LeftDown;
	case WM_LBUTTONUP:
		return WindowsMouseEventType::LeftUp;
	case WM_RBUTTONDOWN:
		return WindowsMouseEventType::RightDown;
	case WM_RBUTTONUP:
		return WindowsMouseEventType::RightUp;
	case WM_MBUTTONDOWN:
		return WindowsMouseEventType::MiddleDown;
	case WM_MBUTTONUP:
		return WindowsMouseEventType::MiddleUp;
	case WM_MOUSEWHEEL:
		return WindowsMouseEventType::Wheel;
	case WM_MOUSEHWHEEL:
		return WindowsMouseEventType::HorizontalWheel;
	case WM_XBUTTONDOWN:
		return WindowsMouseEventType::XButtonDown;
	case WM_XBUTTONUP:
		return WindowsMouseEventType::XButtonUp;
	default:
		return WindowsMouseEventType::Unknown;
	}
}

} // namespace

WindowsMouseObserver::~WindowsMouseObserver() {
	Stop();
}

bool WindowsMouseObserver::Start() noexcept {
	if (m_hook != nullptr) {
		return true;
	}
	if (s_instance != nullptr && s_instance != this) {
		return false;
	}

	s_instance = this;
	m_hook = ::SetWindowsHookExW(
		WH_MOUSE_LL,
		&WindowsMouseObserver::HookProc,
		::GetModuleHandleW(nullptr),
		0
	);
	if (m_hook == nullptr) {
		s_instance = nullptr;
		return false;
	}
	return true;
}

void WindowsMouseObserver::Stop() noexcept {
	if (m_hook != nullptr) {
		::UnhookWindowsHookEx(m_hook);
		m_hook = nullptr;
	}
	if (s_instance == this) {
		s_instance = nullptr;
	}
}

bool WindowsMouseObserver::IsRunning() const noexcept {
	return m_hook != nullptr;
}

std::size_t WindowsMouseObserver::ReadEvents(
	WindowsMouseEvent* output,
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

std::size_t WindowsMouseObserver::DroppedCount() const noexcept {
	return m_events.DroppedCount();
}

LRESULT CALLBACK WindowsMouseObserver::HookProc(
	int code,
	WPARAM wParam,
	LPARAM lParam
) noexcept {
	if (code >= 0 && s_instance != nullptr && lParam != 0) {
		const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
		s_instance->PushEvent(wParam, *info);
	}
	return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

void WindowsMouseObserver::PushEvent(
	WPARAM message,
	const MSLLHOOKSTRUCT& info
) noexcept {
	WindowsMouseEvent event;
	event.timestampTicks = MonotonicClock::NowTicks();
	event.type = ToEventType(message);
	event.x = info.pt.x;
	event.y = info.pt.y;
	event.flags = info.flags;
	event.extraInfo = static_cast<std::uintptr_t>(info.dwExtraInfo);
	if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
		event.wheelDelta = static_cast<std::int16_t>(HIWORD(info.mouseData));
	}
	m_events.TryPush(event);
}

} // namespace qss

#endif
