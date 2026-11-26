#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lucid::core {

// A checked host-side address-space abstraction.
// It does not encode any undocumented console memory layout.
class MemoryBus final {
public:
    [[nodiscard]] bool map_region(std::uint64_t base, std::size_t size,
                                  std::string* error = nullptr);

    [[nodiscard]] bool read(std::uint64_t address,
                            std::span<std::uint8_t> output,
                            std::string* error = nullptr) const;

    [[nodiscard]] bool write(std::uint64_t address,
                             std::span<const std::uint8_t> input,
                             std::string* error = nullptr);

    [[nodiscard]] bool read8(std::uint64_t address, std::uint8_t& value,
                             std::string* error = nullptr) const;

    [[nodiscard]] bool write8(std::uint64_t address, std::uint8_t value,
                              std::string* error = nullptr);

    [[nodiscard]] std::size_t region_count() const noexcept;

private:
    struct Region {
        std::uint64_t base{};
        std::vector<std::uint8_t> bytes;
    };

    [[nodiscard]] Region* find_region(std::uint64_t address) noexcept;
    [[nodiscard]] const Region* find_region(std::uint64_t address) const noexcept;

    std::vector<Region> regions_;
};

} // namespace lucid::core
