#pragma once

#include "lucid/core/memory_bus.hpp"
#include "lucid/mmu/address_space.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lucid::mmu {

// A deliberately limited AArch64 stage-1 walker:
// 4 KiB granule, 48-bit lower VA, 48-bit PA, TTBR0_EL1, EL1 accesses.
// It supports L1/L2 block descriptors and L3 page descriptors.
class Stage1Mmu final {
public:
    static constexpr std::uint64_t PageSize = 4096U;
    static constexpr std::size_t TlbCapacity = 256U;
    static constexpr std::uint64_t TcrT0sz48Bit = 16U;
    static constexpr std::uint64_t TcrIps48Bit = 5ULL << 32U;
    static constexpr std::uint64_t DefaultTcr = TcrIps48Bit | TcrT0sz48Bit;

    explicit Stage1Mmu(core::MemoryBus& physical_memory) noexcept;

    [[nodiscard]] bool configure(std::uint64_t ttbr0_el1,
                                 std::uint64_t tcr_el1) noexcept;
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] Translation translate(std::uint64_t virtual_address,
                                        AccessType access) noexcept;

    void invalidate_all() noexcept;
    void invalidate_va(std::uint64_t virtual_address) noexcept;

    [[nodiscard]] std::size_t tlb_entries() const noexcept;
    [[nodiscard]] std::uint64_t tlb_hits() const noexcept;
    [[nodiscard]] std::uint64_t tlb_misses() const noexcept;
    [[nodiscard]] std::uint64_t ttbr0_el1() const noexcept;
    [[nodiscard]] std::uint64_t tcr_el1() const noexcept;

private:
    struct TlbEntry {
        std::uint64_t virtual_page{};
        std::uint64_t physical_page{};
        bool writable{};
        bool executable{};
        bool valid{};
    };

    [[nodiscard]] Translation walk(std::uint64_t virtual_address,
                                   AccessType access) const noexcept;

    core::MemoryBus& physical_memory_;
    std::uint64_t ttbr0_el1_{};
    std::uint64_t tcr_el1_{};
    std::array<TlbEntry, TlbCapacity> tlb_{};
    std::size_t next_victim_{};
    std::size_t tlb_entries_{};
    std::uint64_t tlb_hits_{};
    std::uint64_t tlb_misses_{};
    bool configured_{};
};

} // namespace lucid::mmu
