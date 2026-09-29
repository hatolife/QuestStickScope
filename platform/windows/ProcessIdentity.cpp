#include "platform/windows/ProcessIdentity.hpp"

#ifdef _WIN32

#include <Windows.h>
#include <bcrypt.h>
#include <TlHelp32.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace qss {
namespace {

std::uint32_t FindProcessId(const std::vector<std::wstring>& executableNames) {
	const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	std::uint32_t processId = 0;
	if (::Process32FirstW(snapshot, &entry)) {
		do {
			for (const std::wstring& name : executableNames) {
				if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
					processId = entry.th32ProcessID;
					break;
				}
			}
			if (processId != 0) {
				break;
			}
		} while (::Process32NextW(snapshot, &entry));
	}
	::CloseHandle(snapshot);
	return processId;
}

std::filesystem::path GetProcessPath(std::uint32_t processId) {
	const HANDLE process = ::OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE,
		processId
	);
	if (process == nullptr) {
		return {};
	}

	std::wstring buffer(32768, L'\0');
	DWORD size = static_cast<DWORD>(buffer.size());
	const BOOL ok = ::QueryFullProcessImageNameW(
		process,
		0,
		buffer.data(),
		&size
	);
	::CloseHandle(process);
	if (!ok) {
		return {};
	}
	buffer.resize(size);
	return std::filesystem::path(buffer);
}

std::string GetFileVersionString(const std::filesystem::path& path) {
	DWORD handle = 0;
	const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
	if (size == 0) {
		return {};
	}

	std::vector<std::uint8_t> data(size);
	if (!::GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
		return {};
	}

	VS_FIXEDFILEINFO* info = nullptr;
	UINT infoSize = 0;
	if (!::VerQueryValueW(
		data.data(),
		L"\\",
		reinterpret_cast<void**>(&info),
		&infoSize
	) || info == nullptr || infoSize < sizeof(VS_FIXEDFILEINFO)) {
		return {};
	}

	std::ostringstream stream;
	stream
		<< HIWORD(info->dwFileVersionMS) << '.'
		<< LOWORD(info->dwFileVersionMS) << '.'
		<< HIWORD(info->dwFileVersionLS) << '.'
		<< LOWORD(info->dwFileVersionLS);
	return stream.str();
}

std::string Sha256File(const std::filesystem::path& path) {
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	std::vector<std::uint8_t> object;
	std::array<std::uint8_t, 32> digest{};
	std::string result;

	if (BCryptOpenAlgorithmProvider(
		&algorithm,
		BCRYPT_SHA256_ALGORITHM,
		nullptr,
		0
	) < 0) {
		return {};
	}

	DWORD objectLength = 0;
	DWORD bytesWritten = 0;
	if (BCryptGetProperty(
		algorithm,
		BCRYPT_OBJECT_LENGTH,
		reinterpret_cast<PUCHAR>(&objectLength),
		sizeof(objectLength),
		&bytesWritten,
		0
	) < 0) {
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return {};
	}
	object.resize(objectLength);

	if (BCryptCreateHash(
		algorithm,
		&hash,
		object.data(),
		static_cast<ULONG>(object.size()),
		nullptr,
		0,
		0
	) < 0) {
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return {};
	}

	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return {};
	}

	std::array<char, 64 * 1024> buffer{};
	while (stream) {
		stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = stream.gcount();
		if (count > 0 && BCryptHashData(
			hash,
			reinterpret_cast<PUCHAR>(buffer.data()),
			static_cast<ULONG>(count),
			0
		) < 0) {
			BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			return {};
		}
	}

	if (BCryptFinishHash(
		hash,
		digest.data(),
		static_cast<ULONG>(digest.size()),
		0
	) >= 0) {
		std::ostringstream output;
		output << std::hex << std::setfill('0');
		for (const std::uint8_t byte : digest) {
			output << std::setw(2) << static_cast<unsigned int>(byte);
		}
		result = output.str();
	}

	BCryptDestroyHash(hash);
	BCryptCloseAlgorithmProvider(algorithm, 0);
	return result;
}

} // namespace

ProcessIdentity InspectProcessIdentity(
	const std::vector<std::wstring>& executableNames
) {
	ProcessIdentity identity;
	identity.processId = FindProcessId(executableNames);
	if (identity.processId == 0) {
		identity.error = "Process not running.";
		return identity;
	}

	identity.running = true;
	identity.executablePath = GetProcessPath(identity.processId);
	if (identity.executablePath.empty()) {
		identity.error = "Failed to query executable path.";
		return identity;
	}

	identity.fileVersion = GetFileVersionString(identity.executablePath);
	identity.sha256 = Sha256File(identity.executablePath);
	if (identity.fileVersion.empty() || identity.sha256.empty()) {
		identity.error = "Some file identity fields could not be read.";
	}
	return identity;
}

} // namespace qss

#endif
