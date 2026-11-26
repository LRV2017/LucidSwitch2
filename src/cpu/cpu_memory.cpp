#include "lucid/cpu/cpu_memory.hpp"

#include <limits>

namespace lucid::cpu {

CpuMemory::CpuMemory(core::MemoryBus& physical_memory,
                     const mmu::AddressSpace* address_space) noexcept
    : physical_memory_(physical_memory), address_space_(address_space) {}

bool CpuMemory::translate_and_validate(
    const std::uint64_t address, const std::size_t size,
    const mmu::AccessType access,
    const core::MemoryPermission physical_permission,
    std::vector<std::uint64_t>& physical_addresses) const {
    physical_addresses.clear();
    if (size == 0U) return true;
    if (size - 1U > std::numeric_limits<std::uint64_t>::max() - address) {
        return false;
    }

    physical_addresses.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const auto virtual_address = address + static_cast<std::uint64_t>(index);
        const auto translated = address_space_->translate(virtual_address, access);
        if (!translated ||
            !physical_memory_.can_access(translated.physical_address, 1U,
                                         physical_permission)) {
            physical_addresses.clear();
            return false;
        }
        physical_addresses.push_back(translated.physical_address);
    }
    return true;
}

bool CpuMemory::read(const std::uint64_t address,
                     const std::span<std::uint8_t> output) const {
    if (address_space_ == nullptr) return physical_memory_.read(address, output);
    std::vector<std::uint64_t> physical_addresses;
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
    if (address_space_ == nullptr) {
        return physical_memory_.read_instruction(address, output);
    }
    std::vector<std::uint64_t> physical_addresses;
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
    if (address_space_ == nullptr) return physical_memory_.write(address, input);
    std::vector<std::uint64_t> physical_addresses;
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

} // namespace lucid::cpu
