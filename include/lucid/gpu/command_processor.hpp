#pragma once

#include "lucid/gpu/graphics_backend.hpp"
#include "lucid/gpu/guest_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace lucid::gpu {

enum class GpuOpcode : std::uint8_t {
    Nop = 0x00U,
    BeginFrame = 0x01U,
    Clear = 0x02U,
    DrawRect = 0x03U,
    Present = 0x04U,
    End = 0xFFU
};

struct GpuProcessorStats {
    std::uint64_t submissions{};
    std::uint64_t commands{};
    std::uint64_t frames_presented{};
};

// Command packet header: opcode in bits 31..24, payload word count in 23..0.
// All payload words are little-endian uint32 values in guest GPU memory.
class GpuCommandProcessor final {
public:
    static constexpr std::size_t MaxCommandBufferBytes = 1024U * 1024U;
    static constexpr std::uint32_t MaxRenderDimension = 8192U;

    [[nodiscard]] bool submit(const GuestGpuMemory& memory,
                              std::uint64_t guest_address,
                              std::size_t byte_length,
                              GraphicsBackend& backend,
                              std::string* error = nullptr);

    [[nodiscard]] const GpuProcessorStats& stats() const noexcept;

private:
    GpuProcessorStats stats_{};
};

[[nodiscard]] constexpr std::uint32_t encode_header(GpuOpcode opcode,
                                                     std::uint32_t payload_words) noexcept {
    return (static_cast<std::uint32_t>(opcode) << 24U) |
           (payload_words & 0x00FFFFFFU);
}

} // namespace lucid::gpu
