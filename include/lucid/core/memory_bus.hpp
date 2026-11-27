#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <shared_mutex>
#include <string>
#include <vector>
namespace lucid::core {
enum class MemoryPermission : std::uint8_t {
    None = 0, Read = 1U, Write = 2U, Execute = 4U,
    ReadWrite = 3U, ReadWriteExecute = 7U
};
[[nodiscard]] constexpr MemoryPermission operator|(MemoryPermission left,
                                                    MemoryPermission right) noexcept {
    return static_cast<MemoryPermission>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}
// Generic checked address space. No console-specific map is implied.
class MemoryBus final {
public:
    static constexpr std::size_t ExclusiveGranuleBytes = 16U;

    [[nodiscard]] bool map_region(std::uint64_t base, std::size_t size,
        std::string* error = nullptr,
        MemoryPermission permissions = MemoryPermission::ReadWriteExecute);
    [[nodiscard]] bool read(std::uint64_t address, std::span<std::uint8_t> output,
        std::string* error = nullptr) const;
    [[nodiscard]] bool read_instruction(std::uint64_t address,
        std::span<std::uint8_t> output, std::string* error = nullptr) const;
    [[nodiscard]] bool write(std::uint64_t address,
        std::span<const std::uint8_t> input, std::string* error = nullptr);

    // Exclusive accesses use a documented 16-byte reservation granule.
    // Mapping, data, and reservation operations are synchronized for
    // concurrent host-thread guest-core execution.
    [[nodiscard]] bool read_exclusive(std::uint64_t address,
        std::span<std::uint8_t> output, std::uint64_t& version,
        std::string* error = nullptr) const;
    [[nodiscard]] bool write_exclusive(std::uint64_t address,
        std::span<const std::uint8_t> input, std::uint64_t expected_version,
        bool& stored, std::string* error = nullptr);
    [[nodiscard]] bool can_access(std::uint64_t address, std::size_t size,
        MemoryPermission permission) const;
    [[nodiscard]] bool read8(std::uint64_t address, std::uint8_t& value,
        std::string* error = nullptr) const;
    [[nodiscard]] bool write8(std::uint64_t address, std::uint8_t value,
        std::string* error = nullptr);
    [[nodiscard]] std::size_t region_count() const;
private:
    struct Region {
        std::uint64_t base{};
        std::vector<std::uint8_t> bytes;
        MemoryPermission permissions{MemoryPermission::ReadWriteExecute};
        std::vector<std::uint64_t> exclusive_versions;
    };
    [[nodiscard]] Region* find_region(std::uint64_t address) noexcept;
    [[nodiscard]] const Region* find_region(std::uint64_t address) const noexcept;
    [[nodiscard]] bool read_with_permission(std::uint64_t address,
        std::span<std::uint8_t> output, MemoryPermission permission,
        std::string* error) const;
    mutable std::shared_mutex mutex_;
    std::vector<Region> regions_;
};
} // namespace lucid::core
