#pragma once

#ifdef _WIN32

#include "ipc/SpscRingBuffer.hpp"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace qss {

enum class WindowsMouseEventType : std::uint8_t {
	Move,
	LeftDown,
	LeftUp,
	RightDown,
	RightUp,
	MiddleDown,
	MiddleUp,
	Wheel,
	HorizontalWheel,
	XButtonDown,
	XButtonUp,
	Unknown,
};

struct WindowsMouseEvent {
	std::int64_t timestampTicks = 0;
	WindowsMouseEventType type = WindowsMouseEventType::Unknown;
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::int32_t wheelDelta = 0;
	std::uint32_t flags = 0;
	std::uintptr_t extraInfo = 0;
};

class WindowsMouseObserver {
public:
	WindowsMouseObserver() = default;
	~WindowsMouseObserver();

	WindowsMouseObserver(const WindowsMouseObserver&) = delete;
	WindowsMouseObserver& operator=(const WindowsMouseObserver&) = delete;

	bool Start() noexcept;
	void Stop() noexcept;
	bool IsRunning() const noexcept;
	std::size_t ReadEvents(WindowsMouseEvent* output, std::size_t capacity) noexcept;
	std::size_t DroppedCount() const noexcept;

private:
	static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) noexcept;
	void PushEvent(WPARAM message, const MSLLHOOKSTRUCT& info) noexcept;

	static WindowsMouseObserver* s_instance;
	HHOOK m_hook = nullptr;
	SpscRingBuffer<WindowsMouseEvent, 4096> m_events;
};

} // namespace qss

#endif
