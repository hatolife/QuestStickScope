#include "platform/windows/SteamVRSharedMemory.hpp"

#ifdef _WIN32

#include "platform/Clock.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace qss {
namespace {

bool IsValidState(const SteamVRSharedState* state) noexcept {
	return state != nullptr &&
		state->magic == kSteamVRSharedMagic &&
		state->version == kSteamVRSharedVersion;
}

} // namespace

SteamVRSharedMemoryWriter::~SteamVRSharedMemoryWriter() {
	Close();
}

bool SteamVRSharedMemoryWriter::Open() noexcept {
	Close();

	m_mapping = ::CreateFileMappingW(
		INVALID_HANDLE_VALUE,
		nullptr,
		PAGE_READWRITE,
		0,
		static_cast<DWORD>(sizeof(SteamVRSharedState)),
		kSteamVRSharedMemoryName
	);
	if (m_mapping == nullptr) {
		return false;
	}

	const bool alreadyExists = (::GetLastError() == ERROR_ALREADY_EXISTS);
	m_state = static_cast<SteamVRSharedState*>(::MapViewOfFile(
		m_mapping,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		sizeof(SteamVRSharedState)
	));
	if (m_state == nullptr) {
		Close();
		return false;
	}

	if (!alreadyExists) {
		new (m_state) SteamVRSharedState{};
		m_state->qpcFrequency = static_cast<std::uint64_t>(MonotonicClock::Frequency());
	} else if (!IsValidState(m_state)) {
		Close();
		return false;
	}

	m_state->probeState.store(static_cast<std::uint32_t>(ProbeState::PassThrough), std::memory_order_release);
	return true;
}

void SteamVRSharedMemoryWriter::Close() noexcept {
	if (m_state != nullptr) {
		m_state->probeState.store(static_cast<std::uint32_t>(ProbeState::Offline), std::memory_order_release);
		::UnmapViewOfFile(m_state);
		m_state = nullptr;
	}
	if (m_mapping != nullptr) {
		::CloseHandle(m_mapping);
		m_mapping = nullptr;
	}
}

bool SteamVRSharedMemoryWriter::IsOpen() const noexcept {
	return m_state != nullptr;
}

void SteamVRSharedMemoryWriter::SetProbeState(ProbeState state) noexcept {
	if (m_state == nullptr) {
		return;
	}
	m_state->probeState.store(static_cast<std::uint32_t>(state), std::memory_order_release);
}

void SteamVRSharedMemoryWriter::SetHeartbeat(std::int64_t timestampTicks) noexcept {
	if (m_state == nullptr) {
		return;
	}
	m_state->heartbeatTicks.store(static_cast<std::uint64_t>(timestampTicks), std::memory_order_release);
}

bool SteamVRSharedMemoryWriter::RegisterScalarComponent(
	std::uint64_t handle,
	std::uint64_t container,
	const char* path,
	std::int32_t scalarType,
	std::int32_t scalarUnits,
	ControllerHand hand,
	ScalarSemantic semantic,
	std::uint32_t& componentIndex
) noexcept {
	if (m_state == nullptr || path == nullptr) {
		return false;
	}

	const std::uint32_t index = m_state->componentCount.fetch_add(1, std::memory_order_acq_rel);
	if (index >= kSteamVRMaxScalarComponents) {
		m_state->componentCount.store(static_cast<std::uint32_t>(kSteamVRMaxScalarComponents), std::memory_order_release);
		return false;
	}

	SharedScalarComponent& component = m_state->components[index];
	const std::uint64_t beginStamp = static_cast<std::uint64_t>(index + 1) * 2ULL - 1ULL;
	component.stamp.store(beginStamp, std::memory_order_release);
	component.handle = handle;
	component.container = container;
	component.scalarType = scalarType;
	component.scalarUnits = scalarUnits;
	component.hand = hand;
	component.semantic = semantic;
	component.path.fill('\0');
	const std::size_t pathLength = std::min(std::strlen(path), component.path.size() - 1);
	std::memcpy(component.path.data(), path, pathLength);
	component.stamp.store(beginStamp + 1ULL, std::memory_order_release);

	componentIndex = index;
	return true;
}

void SteamVRSharedMemoryWriter::WriteScalarSample(const SharedScalarSample& input) noexcept {
	if (m_state == nullptr) {
		return;
	}

	const std::uint64_t sequence = m_state->writeSequence.fetch_add(1, std::memory_order_acq_rel) + 1ULL;
	SharedScalarSampleSlot& slot = m_state->samples[(sequence - 1ULL) % kSteamVRSampleCapacity];
	const std::uint64_t beginStamp = sequence * 2ULL - 1ULL;
	SharedScalarSample sample = input;
	sample.sequence = sequence;

	slot.stamp.store(beginStamp, std::memory_order_release);
	slot.sample = sample;
	slot.stamp.store(beginStamp + 1ULL, std::memory_order_release);
}

SteamVRSharedMemoryReader::~SteamVRSharedMemoryReader() {
	Close();
}

bool SteamVRSharedMemoryReader::Open() noexcept {
	Close();

	m_mapping = ::OpenFileMappingW(FILE_MAP_READ, FALSE, kSteamVRSharedMemoryName);
	if (m_mapping == nullptr) {
		return false;
	}

	m_state = static_cast<const SteamVRSharedState*>(::MapViewOfFile(
		m_mapping,
		FILE_MAP_READ,
		0,
		0,
		sizeof(SteamVRSharedState)
	));
	if (!IsValidState(m_state)) {
		Close();
		return false;
	}

	return true;
}

void SteamVRSharedMemoryReader::Close() noexcept {
	if (m_state != nullptr) {
		::UnmapViewOfFile(m_state);
		m_state = nullptr;
	}
	if (m_mapping != nullptr) {
		::CloseHandle(m_mapping);
		m_mapping = nullptr;
	}
}

bool SteamVRSharedMemoryReader::IsOpen() const noexcept {
	return m_state != nullptr;
}

ProbeState SteamVRSharedMemoryReader::GetProbeState() const noexcept {
	if (m_state == nullptr) {
		return ProbeState::Offline;
	}
	return static_cast<ProbeState>(m_state->probeState.load(std::memory_order_acquire));
}

std::uint64_t SteamVRSharedMemoryReader::GetHeartbeatTicks() const noexcept {
	if (m_state == nullptr) {
		return 0;
	}
	return m_state->heartbeatTicks.load(std::memory_order_acquire);
}

std::uint32_t SteamVRSharedMemoryReader::GetComponentCount() const noexcept {
	if (m_state == nullptr) {
		return 0;
	}
	return std::min<std::uint32_t>(
		m_state->componentCount.load(std::memory_order_acquire),
		static_cast<std::uint32_t>(kSteamVRMaxScalarComponents)
	);
}

bool SteamVRSharedMemoryReader::ReadComponent(std::uint32_t index, ScalarComponentSnapshot& output) const noexcept {
	if (m_state == nullptr || index >= GetComponentCount()) {
		return false;
	}

	const SharedScalarComponent& component = m_state->components[index];
	const std::uint64_t before = component.stamp.load(std::memory_order_acquire);
	if ((before & 1ULL) != 0ULL || before == 0ULL) {
		return false;
	}

	output.handle = component.handle;
	output.container = component.container;
	output.scalarType = component.scalarType;
	output.scalarUnits = component.scalarUnits;
	output.hand = component.hand;
	output.semantic = component.semantic;
	output.path = component.path;
	const std::uint64_t after = component.stamp.load(std::memory_order_acquire);
	return before == after;
}

bool SteamVRSharedMemoryReader::ReadLatestSample(SharedScalarSample& output) const noexcept {
	if (m_state == nullptr) {
		return false;
	}

	const std::uint64_t sequence = m_state->writeSequence.load(std::memory_order_acquire);
	if (sequence == 0ULL) {
		return false;
	}

	const SharedScalarSampleSlot& slot = m_state->samples[(sequence - 1ULL) % kSteamVRSampleCapacity];
	const std::uint64_t before = slot.stamp.load(std::memory_order_acquire);
	if ((before & 1ULL) != 0ULL || before == 0ULL) {
		return false;
	}

	output = slot.sample;
	const std::uint64_t after = slot.stamp.load(std::memory_order_acquire);
	return before == after && output.sequence == sequence;
}

std::size_t SteamVRSharedMemoryReader::ReadSamples(
	std::uint64_t& nextSequence,
	SharedScalarSample* output,
	std::size_t capacity
) const noexcept {
	if (m_state == nullptr || output == nullptr || capacity == 0) {
		return 0;
	}

	const std::uint64_t latest = m_state->writeSequence.load(std::memory_order_acquire);
	if (latest == 0ULL) {
		return 0;
	}

	const std::uint64_t earliest = latest > kSteamVRSampleCapacity
		? latest - kSteamVRSampleCapacity + 1ULL
		: 1ULL;
	if (nextSequence == 0ULL || nextSequence < earliest) {
		nextSequence = earliest;
	}

	std::size_t count = 0;
	while (count < capacity && nextSequence <= latest) {
		const SharedScalarSampleSlot& slot = m_state->samples[(nextSequence - 1ULL) % kSteamVRSampleCapacity];
		const std::uint64_t before = slot.stamp.load(std::memory_order_acquire);
		if ((before & 1ULL) != 0ULL || before == 0ULL) {
			break;
		}

		const SharedScalarSample sample = slot.sample;
		const std::uint64_t after = slot.stamp.load(std::memory_order_acquire);
		if (before != after || sample.sequence != nextSequence) {
			break;
		}

		output[count] = sample;
		++count;
		++nextSequence;
	}

	return count;
}

} // namespace qss

#endif
