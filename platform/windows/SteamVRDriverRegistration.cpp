#ifdef _WIN32

#include "platform/windows/SteamVRDriverRegistration.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace qss {
namespace {

constexpr wchar_t kDriverName[] = L"queststickscope";
constexpr wchar_t kDriverManifest[] = L"driver.vrdrivermanifest";

struct ProcessResult {
	bool started = false;
	DWORD exitCode = 0;
	std::string output;
	std::string error;
};

std::string WideToUtf8(std::wstring_view value) {
	if(value.empty()){ return {}; }
	const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if(size <= 0){ return {}; }
	std::string result(static_cast<std::size_t>(size), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
	return result;
}

std::wstring BytesToWide(const std::string& value) {
	if(value.empty()){ return {}; }
	int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
	UINT codePage = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	if(size <= 0) {
		codePage = CP_ACP;
		flags = 0;
		size = ::MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
	}
	if(size <= 0){ return {}; }
	std::wstring result(static_cast<std::size_t>(size), L'\0');
	::MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), result.data(), size);
	return result;
}

std::wstring Trim(std::wstring value) {
	const auto isSpace = [](wchar_t c){ return std::iswspace(c) != 0; };
	while(!value.empty() && isSpace(value.front())){ value.erase(value.begin()); }
	while(!value.empty() && isSpace(value.back())){ value.pop_back(); }
	if(value.size() >= 2 && value.front() == L'"' && value.back() == L'"'){ value = value.substr(1, value.size() - 2); }
	return value;
}

std::wstring ToLower(std::wstring value) {
	std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c){ return static_cast<wchar_t>(std::towlower(c)); });
	return value;
}

bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) {
	return ToLower(std::wstring(left)) == ToLower(std::wstring(right));
}

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
	if(path.empty()){ return {}; }
	std::error_code error;
	const std::filesystem::path absolute = std::filesystem::absolute(path, error);
	if(error){ return path.lexically_normal(); }
	return absolute.lexically_normal();
}

bool PathsEqual(const std::filesystem::path& left, const std::filesystem::path& right) {
	if(left.empty() || right.empty()){ return false; }
	return EqualsIgnoreCase(NormalizePath(left).native(), NormalizePath(right).native());
}

std::filesystem::path ToDriverRoot(std::wstring value) {
	value = Trim(std::move(value));
	if(value.empty()){ return {}; }
	std::filesystem::path path(value);
	if(EqualsIgnoreCase(path.filename().native(), kDriverManifest)){ path = path.parent_path(); }
	return NormalizePath(path);
}

std::filesystem::path GetExecutableDirectory() {
	std::vector<wchar_t> buffer(32768, L'\0');
	const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if(length == 0 || length >= buffer.size()){ return {}; }
	return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::filesystem::path ResolveDriverRoot() {
	const std::filesystem::path executableDirectory = GetExecutableDirectory();
	if(executableDirectory.empty()){ return {}; }
	const std::filesystem::path driverRoot = executableDirectory / L"steamvr-driver" / kDriverName;
	const std::filesystem::path manifest = driverRoot / kDriverManifest;
	const std::filesystem::path driverDll = driverRoot / L"bin" / L"win64" / L"driver_queststickscope.dll";
	if(!std::filesystem::is_regular_file(manifest) || !std::filesystem::is_regular_file(driverDll)){ return {}; }
	return NormalizePath(driverRoot);
}

bool ReadRegistryString(HKEY root, const wchar_t* subKey, REGSAM view, const wchar_t* valueName, std::wstring& value) {
	HKEY key = nullptr;
	const LSTATUS openResult = ::RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | view, &key);
	if(openResult != ERROR_SUCCESS){ return false; }

	DWORD type = 0;
	DWORD byteCount = 0;
	const LSTATUS sizeResult = ::RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteCount);
	if(sizeResult != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || byteCount == 0) {
		::RegCloseKey(key);
		return false;
	}

	std::vector<wchar_t> buffer(byteCount / sizeof(wchar_t) + 1, L'\0');
	const LSTATUS readResult = ::RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &byteCount);
	::RegCloseKey(key);
	if(readResult != ERROR_SUCCESS){ return false; }

	value.assign(buffer.data());
	if(type == REG_EXPAND_SZ) {
		const DWORD expandedSize = ::ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
		if(expandedSize > 0) {
			std::vector<wchar_t> expanded(expandedSize, L'\0');
			if(::ExpandEnvironmentStringsW(value.c_str(), expanded.data(), expandedSize) != 0){ value.assign(expanded.data()); }
		}
	}
	return !value.empty();
}

bool HasVrPathReg(const std::filesystem::path& steamVrRoot) {
	return std::filesystem::is_regular_file(steamVrRoot / L"bin" / L"win64" / L"vrpathreg.exe");
}

std::filesystem::path ResolveSteamVRRoot() {
	const wchar_t* uninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 250820";
	for(const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
		std::wstring installLocation;
		if(ReadRegistryString(HKEY_LOCAL_MACHINE, uninstallKey, view, L"InstallLocation", installLocation)) {
			const std::filesystem::path candidate = NormalizePath(installLocation);
			if(HasVrPathReg(candidate)){ return candidate; }
		}
	}

	std::vector<wchar_t> programFiles(32768, L'\0');
	const DWORD length = ::GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles.data(), static_cast<DWORD>(programFiles.size()));
	if(length > 0 && length < programFiles.size()) {
		const std::filesystem::path candidate = std::filesystem::path(std::wstring(programFiles.data(), length)) / L"Steam" / L"steamapps" / L"common" / L"SteamVR";
		if(HasVrPathReg(candidate)){ return NormalizePath(candidate); }
	}
	return {};
}

std::wstring QuoteArgument(const std::wstring& argument) {
	if(argument.find_first_of(L" \t\"") == std::wstring::npos){ return argument; }

	std::wstring result = L"\"";
	std::size_t backslashes = 0;
	for(const wchar_t c : argument) {
		if(c == L'\\') {
			++backslashes;
			continue;
		}
		if(c == L'"') {
			result.append(backslashes * 2 + 1, L'\\');
			result.push_back(L'"');
			backslashes = 0;
			continue;
		}
		result.append(backslashes, L'\\');
		backslashes = 0;
		result.push_back(c);
	}
	result.append(backslashes * 2, L'\\');
	result.push_back(L'"');
	return result;
}

ProcessResult RunProcess(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments) {
	ProcessResult result;

	SECURITY_ATTRIBUTES securityAttributes{};
	securityAttributes.nLength = sizeof(securityAttributes);
	securityAttributes.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if(!::CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
		result.error = "CreatePipe failed: " + std::to_string(::GetLastError());
		return result;
	}
	if(!::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
		result.error = "SetHandleInformation failed: " + std::to_string(::GetLastError());
		::CloseHandle(readPipe);
		::CloseHandle(writePipe);
		return result;
	}

	std::wstring commandLine = QuoteArgument(executable.native());
	for(const std::wstring& argument : arguments) {
		commandLine += L" ";
		commandLine += QuoteArgument(argument);
	}

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESTDHANDLES;
	startupInfo.hStdOutput = writePipe;
	startupInfo.hStdError = writePipe;
	startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

	PROCESS_INFORMATION processInfo{};
	std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
	mutableCommandLine.push_back(L'\0');
	const BOOL created = ::CreateProcessW(
		executable.c_str(),
		mutableCommandLine.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		executable.parent_path().c_str(),
		&startupInfo,
		&processInfo
	);
	::CloseHandle(writePipe);
	if(!created) {
		result.error = "CreateProcessW failed: " + std::to_string(::GetLastError());
		::CloseHandle(readPipe);
		return result;
	}
	result.started = true;

	std::array<char, 4096> buffer{};
	for(;;) {
		DWORD bytesRead = 0;
		if(!::ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) || bytesRead == 0){ break; }
		result.output.append(buffer.data(), bytesRead);
	}
	::CloseHandle(readPipe);

	::WaitForSingleObject(processInfo.hProcess, INFINITE);
	if(!::GetExitCodeProcess(processInfo.hProcess, &result.exitCode)){ result.error = "GetExitCodeProcess failed: " + std::to_string(::GetLastError()); }
	::CloseHandle(processInfo.hThread);
	::CloseHandle(processInfo.hProcess);
	return result;
}

std::int64_t GetSignedExitCode(DWORD exitCode) {
	return exitCode >= 0x80000000U ? static_cast<std::int64_t>(exitCode) - 0x100000000LL : static_cast<std::int64_t>(exitCode);
}

std::filesystem::path ParseFindDriverPath(const ProcessResult& result) {
	return ToDriverRoot(BytesToWide(result.output));
}

std::vector<std::filesystem::path> ParseQuestStickScopePaths(const std::string& output) {
	std::vector<std::filesystem::path> paths;
	const std::wstring text = BytesToWide(output);
	std::size_t offset = 0;
	while(offset <= text.size()) {
		const std::size_t end = text.find_first_of(L"\r\n", offset);
		const std::wstring line = Trim(text.substr(offset, end == std::wstring::npos ? std::wstring::npos : end - offset));
		if(!line.empty()) {
			const std::size_t separator = line.find(L" : ");
			if(separator != std::wstring::npos) {
				const std::wstring label = Trim(line.substr(0, separator));
				const std::filesystem::path path = ToDriverRoot(line.substr(separator + 3));
				if(!path.empty() && (EqualsIgnoreCase(label, kDriverName) || EqualsIgnoreCase(path.filename().native(), kDriverName))) {
					const bool duplicate = std::any_of(paths.begin(), paths.end(), [&](const std::filesystem::path& existing){ return PathsEqual(existing, path); });
					if(!duplicate){ paths.push_back(path); }
				}
			}
		}
		if(end == std::wstring::npos){ break; }
		offset = end + 1;
		while(offset < text.size() && (text[offset] == L'\r' || text[offset] == L'\n')){ ++offset; }
	}
	return paths;
}

bool RunVrPathReg(const std::filesystem::path& vrPathReg, const std::vector<std::wstring>& arguments, ProcessResult& result, std::string& error) {
	result = RunProcess(vrPathReg, arguments);
	if(!result.started) {
		error = "Failed to start vrpathreg.exe: " + result.error;
		return false;
	}
	if(!result.error.empty()) {
		error = "vrpathreg.exe failed: " + result.error;
		return false;
	}
	return true;
}

bool RemoveRegistrationPath(const std::filesystem::path& vrPathReg, const std::filesystem::path& driverRoot, std::string& error) {
	ProcessResult removeResult;
	if(!RunVrPathReg(vrPathReg, {L"removedriver", driverRoot.native()}, removeResult, error)){ return false; }
	if(GetSignedExitCode(removeResult.exitCode) != 0) {
		error = "vrpathreg.exe removedriver failed for " + WideToUtf8(driverRoot.native()) + " with exit code " + std::to_string(GetSignedExitCode(removeResult.exitCode)) + ".";
		return false;
	}
	return true;
}

} // namespace

SteamVRDriverRegistrationResult EnsureSteamVRDriverRegistration() {
	SteamVRDriverRegistrationResult result;
	result.driverRoot = ResolveDriverRoot();
	if(result.driverRoot.empty()) {
		result.message = "SteamVR driver files were not found next to QuestStickScope.exe.";
		return result;
	}

	const std::filesystem::path steamVrRoot = ResolveSteamVRRoot();
	if(steamVrRoot.empty()) {
		result.message = "SteamVR installation was not found.";
		return result;
	}
	const std::filesystem::path vrPathReg = steamVrRoot / L"bin" / L"win64" / L"vrpathreg.exe";

	ProcessResult findResult;
	std::string error;
	if(!RunVrPathReg(vrPathReg, {L"finddriver", kDriverName}, findResult, error)) {
		result.message = error;
		return result;
	}
	const std::int64_t findExitCode = GetSignedExitCode(findResult.exitCode);
	if(findExitCode != 0 && findExitCode != 1 && findExitCode != 2) {
		result.message = "vrpathreg.exe finddriver failed with exit code " + std::to_string(findExitCode) + ".";
		return result;
	}

	const std::filesystem::path foundDriverRoot = findExitCode == 0 ? ParseFindDriverPath(findResult) : std::filesystem::path{};
	result.registeredDriverRoot = foundDriverRoot;

	ProcessResult showResult;
	if(!RunVrPathReg(vrPathReg, {L"show"}, showResult, error)) {
		result.message = error;
		return result;
	}
	if(GetSignedExitCode(showResult.exitCode) != 0) {
		result.message = "vrpathreg.exe show failed with exit code " + std::to_string(GetSignedExitCode(showResult.exitCode)) + ".";
		return result;
	}

	const std::vector<std::filesystem::path> knownPaths = ParseQuestStickScopePaths(showResult.output);
	const bool hasOldPath = std::any_of(knownPaths.begin(), knownPaths.end(), [&](const std::filesystem::path& path){ return !PathsEqual(path, result.driverRoot); });
	const bool currentRegistration = findExitCode == 0 && PathsEqual(foundDriverRoot, result.driverRoot) && !hasOldPath;
	if(currentRegistration) {
		result.success = true;
		result.message = "SteamVR driver registration is current.";
		return result;
	}

	if(findExitCode == 0 || findExitCode == 2) {
		ProcessResult removeByNameResult;
		if(!RunVrPathReg(vrPathReg, {L"removedriverswithname", kDriverName}, removeByNameResult, error)) {
			result.message = error;
			return result;
		}
		if(GetSignedExitCode(removeByNameResult.exitCode) != 0) {
			result.message = "vrpathreg.exe removedriverswithname failed with exit code " + std::to_string(GetSignedExitCode(removeByNameResult.exitCode)) + ".";
			return result;
		}
	}

	ProcessResult remainingResult;
	if(!RunVrPathReg(vrPathReg, {L"show"}, remainingResult, error)) {
		result.message = error;
		return result;
	}
	if(GetSignedExitCode(remainingResult.exitCode) != 0) {
		result.message = "vrpathreg.exe show failed after cleanup with exit code " + std::to_string(GetSignedExitCode(remainingResult.exitCode)) + ".";
		return result;
	}
	for(const std::filesystem::path& path : ParseQuestStickScopePaths(remainingResult.output)) {
		if(!RemoveRegistrationPath(vrPathReg, path, error)) {
			result.message = error;
			return result;
		}
	}

	ProcessResult addResult;
	if(!RunVrPathReg(vrPathReg, {L"adddriver", result.driverRoot.native()}, addResult, error)) {
		result.message = error;
		return result;
	}
	if(GetSignedExitCode(addResult.exitCode) != 0) {
		result.message = "vrpathreg.exe adddriver failed with exit code " + std::to_string(GetSignedExitCode(addResult.exitCode)) + ".";
		return result;
	}

	ProcessResult verifyResult;
	if(!RunVrPathReg(vrPathReg, {L"finddriver", kDriverName}, verifyResult, error)) {
		result.message = error;
		return result;
	}
	if(GetSignedExitCode(verifyResult.exitCode) != 0) {
		result.message = "SteamVR driver registration verification failed.";
		return result;
	}
	result.registeredDriverRoot = ParseFindDriverPath(verifyResult);
	if(!PathsEqual(result.registeredDriverRoot, result.driverRoot)) {
		result.message = "SteamVR driver registration points to an unexpected path: " + WideToUtf8(result.registeredDriverRoot.native());
		return result;
	}

	result.success = true;
	result.changed = true;
	result.message = "SteamVR driver registration was updated automatically. Restart SteamVR if it is running.";
	return result;
}

} // namespace qss

#endif
