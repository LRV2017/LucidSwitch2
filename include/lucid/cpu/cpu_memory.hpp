#pragma once

#include "lucid/core/memory_bus.hpp"
#include "lucid/mmu/address_space.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace lucid::cpu {

// CPU-visible memory facade. When an AddressSpace is supplied, every byte is
// translated and permission-checked before reaching the host-side MemoryBus.
class CpuMemory final {
public:
    explicit CpuMemory(core::MemoryBus& physical_memory,
                       const mmu::AddressSpace* address_space = nullptr) noexcept;

    [[nodiscard]] bool read(std::uint64_t address,
                            std::span<std::uint8_t> output) const;
    [[nodiscard]] bool read_instruction(std::uint64_t address,
                                        std::span<std::uint8_t> output) const;
    [[nodiscard]] bool write(std::uint64_t address,
                             std::span<const std::uint8_t> input);

private:
    [[nodiscard]] bool translate_and_validate(
        std::uint64_t address, std::size_t size, mmu::AccessType access,
        core::MemoryPermission physical_permission,
        std::vector<std::uint64_t>& physical_addresses) const;

    core::MemoryBus& physical_memory_;
    const mmu::AddressSpace* address_space_;
};

} // namespace lucid::cpu
