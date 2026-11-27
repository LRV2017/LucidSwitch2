#pragma once

#include "lucid/core/memory_bus.hpp"
#include "lucid/mmu/address_space.hpp"
#include "lucid/mmu/stage1_mmu.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace lucid::cpu {

// CPU-visible memory facade. When an AddressSpace is supplied, every byte is
// translated and permission-checked before reaching the host-side MemoryBus.
class CpuMemory final {
public:
    explicit CpuMemory(core::MemoryBus& physical_memory,
                       const mmu::AddressSpace* address_space = nullptr) noexcept;
    CpuMemory(core::MemoryBus& physical_memory, mmu::Stage1Mmu& stage1_mmu) noexcept;

    [[nodiscard]] bool read(std::uint64_t address,
                            std::span<std::uint8_t> output) const;
    [[nodiscard]] bool read_instruction(std::uint64_t address,
                                        std::span<std::uint8_t> output) const;
    [[nodiscard]] bool write(std::uint64_t address,
                             std::span<const std::uint8_t> input);
    [[nodiscard]] bool atomic_rmw(std::uint64_t address, std::size_t size,
        std::uint64_t operand, core::AtomicOperation operation,
        std::uint64_t& previous_value);
    [[nodiscard]] bool atomic_compare_exchange(std::uint64_t address,
        std::size_t size, std::uint64_t expected, std::uint64_t desired,
        std::uint64_t& previous_value, bool& exchanged);
    [[nodiscard]] bool read_exclusive(std::uint64_t address,
        std::span<std::uint8_t> output, std::uint64_t& physical_address,
        std::uint64_t& version) const;
    [[nodiscard]] bool write_exclusive(std::uint64_t address,
        std::span<const std::uint8_t> input,
        std::uint64_t expected_physical_address,
        std::uint64_t expected_version, bool& stored);

private:
    [[nodiscard]] bool translate_and_validate(
        std::uint64_t address, std::size_t size, mmu::AccessType access,
        core::MemoryPermission physical_permission,
        std::array<std::uint64_t, 8>& physical_addresses) const;

    core::MemoryBus& physical_memory_;
    const mmu::AddressSpace* address_space_;
    mmu::Stage1Mmu* stage1_mmu_{};
};

} // namespace lucid::cpu
