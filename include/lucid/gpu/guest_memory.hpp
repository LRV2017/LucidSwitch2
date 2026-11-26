#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lucid::gpu {

// A bounded, guest-visible GPU address range. The first implementation uses
// host-backed storage; it is not a console's physical VRAM implementation.
class GuestGpuMemory final {
public:
    GuestGpuMemory(std::uint64_t guest_base, std::size_t size);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t guest_base() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool read(std::uint64_t address,
                            std::span<std::uint8_t> output) const noexcept;
    [[nodiscard]] bool write(std::uint64_t address,
                             std::span<const std::uint8_t> input) noexcept;
    [[nodiscard]] bool fill(std::uint64_t address, std::size_t length,
                            std::uint8_t value) noexcept;

private:
    [[nodiscard]] bool offset_of(std::uint64_t address, std::size_t length,
                                 std::size_t& offset) const noexcept;

    std::uint64_t guest_base_{};
    std::vector<std::uint8_t> storage_{};
    bool valid_{};
};

} // namespace lucid::gpu
