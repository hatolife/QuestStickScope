#pragma once

#ifdef _WIN32

#include "ipc/SpscRingBuffer.hpp"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace qss {

struct WindowsRawMouseEvent {
	std::int64_t timestampTicks = 0;
	std::uintptr_t device = 0;
	std::int32_t deltaX = 0;
	std::int32_t deltaY = 0;
	std::int32_t wheelDelta = 0;
	std::uint16_t buttonFlags = 0;
	std::uint16_t rawButtonData = 0;
	std::uint16_t mouseFlags = 0;
};

class WindowsRawInputObserver {
public:
	WindowsRawInputObserver() = default;
	~WindowsRawInputObserver();

	WindowsRawInputObserver(const WindowsRawInputObserver&) = delete;
	WindowsRawInputObserver& operator=(const WindowsRawInputObserver&) = delete;

	bool Start(HWND targetWindow) noexcept;
	void Stop() noexcept;
	bool IsRunning() const noexcept;
	std::size_t ReadEvents(WindowsRawMouseEvent* output, std::size_t capacity) noexcept;
	std::size_t DroppedCount() const noexcept;

	static void HandleWindowMessage(UINT message, WPARAM wParam, LPARAM lParam) noexcept;

private:
	void HandleRawInput(HRAWINPUT rawInput) noexcept;

	static WindowsRawInputObserver* s_instance;
	HWND m_targetWindow = nullptr;
	SpscRingBuffer<WindowsRawMouseEvent, 4096> m_events;
};

} // namespace qss

#endif
