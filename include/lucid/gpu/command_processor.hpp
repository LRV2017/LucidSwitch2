#pragma once

#include "lucid/gpu/graphics_backend.hpp"
#include "lucid/gpu/guest_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace lucid::gpu {

enum class GpuOpcode : std::uint8_t {
    Nop = 0x00U,
    BeginFrame = 0x01U,
    Clear = 0x02U,
    DrawRect = 0x03U,
    Present = 0x04U,
    CreateTexture = 0x05U,
    DestroyTexture = 0x06U,
    DrawIndexed = 0x07U,
    ClearDepth = 0x08U,
    End = 0xFFU
};

struct GpuProcessorStats {
    std::uint64_t submissions{};
    std::uint64_t commands{};
    std::uint64_t frames_presented{};
    std::uint64_t indexed_draws{};
    std::uint64_t textures_created{};
    std::uint64_t textures_destroyed{};
};

// Packet header: opcode in bits 31..24, payload word count in 23..0.
// Payloads and guest resource data use little-endian 32-bit words.
class GpuCommandProcessor final {
public:
    static constexpr std::size_t MaxCommandBufferBytes = 1024U * 1024U;
    static constexpr std::uint32_t MaxRenderDimension = 8192U;
    static constexpr std::uint32_t MaxVerticesPerDraw = 65536U;
    static constexpr std::uint32_t MaxIndicesPerDraw = 196608U;

    [[nodiscard]] bool submit(const GuestGpuMemory& memory,
                              std::uint64_t guest_address,
                              std::size_t byte_length,
                              GraphicsBackend& backend,
                              std::string* error = nullptr);

    [[nodiscard]] const GpuProcessorStats& stats() const noexcept;
    [[nodiscard]] std::size_t live_texture_count() const noexcept;

private:
    GpuProcessorStats stats_{};
    std::unordered_set<TextureId> texture_ids_{};
};

[[nodiscard]] constexpr std::uint32_t encode_header(GpuOpcode opcode,
                                                     std::uint32_t payload_words) noexcept {
    return (static_cast<std::uint32_t>(opcode) << 24U) |
           (payload_words & 0x00FFFFFFU);
}

} // namespace lucid::gpu
