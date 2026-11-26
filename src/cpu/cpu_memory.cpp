#include "lucid/cpu/cpu_memory.hpp"

#include <array>
#include <limits>

namespace lucid::cpu {

CpuMemory::CpuMemory(core::MemoryBus& physical_memory,
                     const mmu::AddressSpace* address_space) noexcept
    : physical_memory_(physical_memory), address_space_(address_space) {}

CpuMemory::CpuMemory(core::MemoryBus& physical_memory,
                     mmu::Stage1Mmu& stage1_mmu) noexcept
    : physical_memory_(physical_memory), address_space_(nullptr),
      stage1_mmu_(&stage1_mmu) {}

bool CpuMemory::translate_and_validate(
    const std::uint64_t address, const std::size_t size,
    const mmu::AccessType access,
    const core::MemoryPermission physical_permission,
    std::array<std::uint64_t, 8>& physical_addresses) const {
    physical_addresses.fill(0U);
    if (size == 0U) return true;
    if (size > physical_addresses.size()) return false;
    if (size - 1U > std::numeric_limits<std::uint64_t>::max() - address) {
        return false;
    }

    for (std::size_t index = 0; index < size; ++index) {
        const auto virtual_address = address + static_cast<std::uint64_t>(index);
        const auto translated = stage1_mmu_ != nullptr
            ? stage1_mmu_->translate(virtual_address, access)
            : address_space_->translate(virtual_address, access);
        if (!translated ||
            !physical_memory_.can_access(translated.physical_address, 1U,
                                         physical_permission)) {
            return false;
        }
        physical_addresses[index] = translated.physical_address;
    }
    return true;
}

bool CpuMemory::read(const std::uint64_t address,
                     const std::span<std::uint8_t> output) const {
    if (address_space_ == nullptr && stage1_mmu_ == nullptr)
        return physical_memory_.read(address, output);
    std::array<std::uint64_t, 8> physical_addresses{};
    if (!translate_and_validate(address, output.size(), mmu::AccessType::Read,
                                core::MemoryPermission::Read,
                                physical_addresses)) {
        return false;
    }
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (!physical_memory_.read8(physical_addresses[index], output[index])) {
            return false;
        }
    }
    return true;
}

bool CpuMemory::read_instruction(const std::uint64_t address,
                                 const std::span<std::uint8_t> output) const {
    if (address_space_ == nullptr && stage1_mmu_ == nullptr) {
        return physical_memory_.read_instruction(address, output);
    }
    std::array<std::uint64_t, 8> physical_addresses{};
    if (!translate_and_validate(address, output.size(), mmu::AccessType::Execute,
                                core::MemoryPermission::Execute,
                                physical_addresses)) {
        return false;
    }
    for (std::size_t index = 0; index < output.size(); ++index) {
        const std::span<std::uint8_t> byte(&output[index], 1U);
        if (!physical_memory_.read_instruction(physical_addresses[index], byte)) {
            return false;
        }
    }
    return true;
}

bool CpuMemory::write(const std::uint64_t address,
                      const std::span<const std::uint8_t> input) {
    if (address_space_ == nullptr && stage1_mmu_ == nullptr)
        return physical_memory_.write(address, input);
    std::array<std::uint64_t, 8> physical_addresses{};
    if (!translate_and_validate(address, input.size(), mmu::AccessType::Write,
                                core::MemoryPermission::Write,
                                physical_addresses)) {
        return false;
    }

    // All bytes are preflighted before any write occurs, so a translation or
    // backing-memory permission fault cannot leave a partially committed store.
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (!physical_memory_.write8(physical_addresses[index], input[index])) {
            return false;
        }
    }
    return true;
}

bool CpuMemory::read_exclusive(const std::uint64_t address,
    const std::span<std::uint8_t> output, std::uint64_t& physical_address,
    std::uint64_t& version) const {
    if (output.size() != 4U && output.size() != 8U) return false;
    if (address_space_ == nullptr && stage1_mmu_ == nullptr) {
        physical_address = address;
        return physical_memory_.read_exclusive(address, output, version);
    }
    std::array<std::uint64_t, 8> physical_addresses{};
    if (!translate_and_validate(address, output.size(), mmu::AccessType::Read,
                                core::MemoryPermission::Read,
                                physical_addresses)) return false;
    for (std::size_t index = 1U; index < output.size(); ++index) {
        if (physical_addresses[index] != physical_addresses[0] + index)
            return false;
    }
    physical_address = physical_addresses[0];
    return physical_memory_.read_exclusive(physical_address, output, version);
}

bool CpuMemory::write_exclusive(const std::uint64_t address,
    const std::span<const std::uint8_t> input,
    const std::uint64_t expected_physical_address,
    const std::uint64_t expected_version, bool& stored) {
    stored = false;
    if (input.size() != 4U && input.size() != 8U) return false;
    if (address_space_ == nullptr && stage1_mmu_ == nullptr) {
        if (address != expected_physical_address) return true;
        return physical_memory_.write_exclusive(
            address, input, expected_version, stored);
    }
    std::array<std::uint64_t, 8> physical_addresses{};
    if (!translate_and_validate(address, input.size(), mmu::AccessType::Write,
                                core::MemoryPermission::Write,
                                physical_addresses)) return false;
    if (physical_addresses[0] != expected_physical_address) return true;
    for (std::size_t index = 1U; index < input.size(); ++index) {
        if (physical_addresses[index] != physical_addresses[0] + index)
            return false;
    }
    return physical_memory_.write_exclusive(
        physical_addresses[0], input, expected_version, stored);
}

} // namespace lucid::cpu
