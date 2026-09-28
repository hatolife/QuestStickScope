#ifdef _WIN32

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace {

constexpr std::uint32_t kProbeAbiVersion = 1;
std::atomic<bool> g_processAttached{false};

} // namespace

extern "C" __declspec(dllexport) std::uint32_t QuestStickScopeProbeGetAbiVersion() noexcept {
	return kProbeAbiVersion;
}

extern "C" __declspec(dllexport) bool QuestStickScopeProbeIsPassThrough() noexcept {
	return true;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
	(void)instance;
	(void)reserved;

	if (reason == DLL_PROCESS_ATTACH) {
		g_processAttached.store(true, std::memory_order_release);
	} else if (reason == DLL_PROCESS_DETACH) {
		g_processAttached.store(false, std::memory_order_release);
	}

	return TRUE;
}

#endif
