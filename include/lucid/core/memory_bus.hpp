#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
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
    [[nodiscard]] bool map_region(std::uint64_t base, std::size_t size,
        std::string* error = nullptr,
        MemoryPermission permissions = MemoryPermission::ReadWriteExecute);
    [[nodiscard]] bool read(std::uint64_t address, std::span<std::uint8_t> output,
        std::string* error = nullptr) const;
    [[nodiscard]] bool read_instruction(std::uint64_t address,
        std::span<std::uint8_t> output, std::string* error = nullptr) const;
    [[nodiscard]] bool write(std::uint64_t address,
        std::span<const std::uint8_t> input, std::string* error = nullptr);
    [[nodiscard]] bool read8(std::uint64_t address, std::uint8_t& value,
        std::string* error = nullptr) const;
    [[nodiscard]] bool write8(std::uint64_t address, std::uint8_t value,
        std::string* error = nullptr);
    [[nodiscard]] std::size_t region_count() const noexcept;
private:
    struct Region {
        std::uint64_t base{};
        std::vector<std::uint8_t> bytes;
        MemoryPermission permissions{MemoryPermission::ReadWriteExecute};
    };
    [[nodiscard]] Region* find_region(std::uint64_t address) noexcept;
    [[nodiscard]] const Region* find_region(std::uint64_t address) const noexcept;
    [[nodiscard]] bool read_with_permission(std::uint64_t address,
        std::span<std::uint8_t> output, MemoryPermission permission,
        std::string* error) const;
    std::vector<Region> regions_;
};
} // namespace lucid::core
