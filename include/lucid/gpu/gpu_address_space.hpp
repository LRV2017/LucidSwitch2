#pragma once

#include "lucid/core/memory_bus.hpp"
#include "lucid/gpu/guest_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>

namespace lucid::gpu {

// Sparse GPU virtual mappings backed by shared guest physical memory.
// This is generic MMU infrastructure, not a Tegra-specific page-table model.
class GpuAddressSpace final : public GpuMemoryView {
public:
    static constexpr std::uint64_t PageSize = 4096U;
    static constexpr std::uint64_t MaximumGpuVirtualAddress = 1ULL << 40U;

    explicit GpuAddressSpace(core::MemoryBus& physical_memory) noexcept;

    [[nodiscard]] bool map(std::uint64_t gpu_address, std::uint64_t physical_address,
                           std::size_t size, core::MemoryPermission permissions,
                           std::string* error = nullptr);
    [[nodiscard]] bool unmap(std::uint64_t gpu_address, std::size_t size,
                             std::string* error = nullptr);
    [[nodiscard]] bool protect(std::uint64_t gpu_address, std::size_t size,
                               core::MemoryPermission permissions,
                               std::string* error = nullptr);

    [[nodiscard]] bool valid() const override;
    [[nodiscard]] bool read(std::uint64_t address,
                            std::span<std::uint8_t> output) const override;
    [[nodiscard]] bool write(std::uint64_t address,
                             std::span<const std::uint8_t> input);
    [[nodiscard]] std::size_t mapping_count() const;

private:
    struct Mapping {
        std::uint64_t gpu_base{};
        std::uint64_t physical_base{};
        std::size_t size{};
        core::MemoryPermission permissions{core::MemoryPermission::None};
    };

    [[nodiscard]] const Mapping* find_mapping(std::uint64_t address) const noexcept;
    [[nodiscard]] bool validate_range(std::uint64_t address, std::size_t size,
                                      core::MemoryPermission required) const;

    core::MemoryBus& physical_memory_;
    mutable std::shared_mutex mutex_;
    std::vector<Mapping> mappings_;
};

} // namespace lucid::gpu
