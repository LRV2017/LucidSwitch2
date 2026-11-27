#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lucid::gpu {

class GpuMemoryView {
public:
    virtual ~GpuMemoryView() = default;
    [[nodiscard]] virtual bool valid() const = 0;
    [[nodiscard]] virtual bool read(std::uint64_t address,
                                    std::span<std::uint8_t> output) const = 0;
};

// A bounded, guest-visible GPU address range. The first implementation uses
// host-backed storage; it is not a console's physical VRAM implementation.
class GuestGpuMemory final : public GpuMemoryView {
public:
    GuestGpuMemory(std::uint64_t guest_base, std::size_t size);

    [[nodiscard]] bool valid() const noexcept override;
    [[nodiscard]] std::uint64_t guest_base() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool read(std::uint64_t address,
                            std::span<std::uint8_t> output) const noexcept override;
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
