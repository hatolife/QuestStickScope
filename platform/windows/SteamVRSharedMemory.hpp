#pragma once

#ifdef _WIN32

#include "ipc/SharedProtocol.hpp"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace qss {

class SteamVRSharedMemoryWriter {
public:
	SteamVRSharedMemoryWriter() = default;
	~SteamVRSharedMemoryWriter();

	SteamVRSharedMemoryWriter(const SteamVRSharedMemoryWriter&) = delete;
	SteamVRSharedMemoryWriter& operator=(const SteamVRSharedMemoryWriter&) = delete;

	bool Open() noexcept;
	void Close() noexcept;
	bool IsOpen() const noexcept;

	void SetProbeState(ProbeState state) noexcept;
	void SetHeartbeat(std::int64_t timestampTicks) noexcept;
	bool RegisterScalarComponent(
		std::uint64_t handle,
		std::uint64_t container,
		const char* path,
		std::int32_t scalarType,
		std::int32_t scalarUnits,
		ControllerHand hand,
		ScalarSemantic semantic,
		std::uint32_t& componentIndex
	) noexcept;
	void WriteScalarSample(const SharedScalarSample& sample) noexcept;

private:
	HANDLE m_mapping = nullptr;
	SteamVRSharedState* m_state = nullptr;
};

class SteamVRSharedMemoryReader {
public:
	SteamVRSharedMemoryReader() = default;
	~SteamVRSharedMemoryReader();

	SteamVRSharedMemoryReader(const SteamVRSharedMemoryReader&) = delete;
	SteamVRSharedMemoryReader& operator=(const SteamVRSharedMemoryReader&) = delete;

	bool Open() noexcept;
	void Close() noexcept;
	bool IsOpen() const noexcept;
	std::uint64_t GetSessionId() const noexcept;
	ProbeState GetProbeState() const noexcept;
	std::uint64_t GetHeartbeatTicks() const noexcept;
	std::uint32_t GetComponentCount() const noexcept;
	bool ReadComponent(std::uint32_t index, ScalarComponentSnapshot& output) const noexcept;
	bool ReadLatestSample(SharedScalarSample& output) const noexcept;
	std::size_t ReadSamples(
		std::uint64_t& nextSequence,
		SharedScalarSample* output,
		std::size_t capacity
	) const noexcept;

private:
	HANDLE m_mapping = nullptr;
	const SteamVRSharedState* m_state = nullptr;
};

} // namespace qss

#endif
