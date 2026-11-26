#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace lucid::gpu {

struct GraphicsRect {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

enum class TextureFilter : std::uint8_t { Nearest, Linear };
enum class TextureAddress : std::uint8_t { Clamp, Repeat };

struct SamplerDesc {
    TextureFilter min_filter{TextureFilter::Nearest};
    TextureFilter mag_filter{TextureFilter::Nearest};
    TextureAddress address_u{TextureAddress::Clamp};
    TextureAddress address_v{TextureAddress::Clamp};
};

using TextureId = std::uint32_t;

// Vertex position is in framebuffer pixel coordinates; depth is in [0,1].
// Colors and texture pixels use packed 0xRRGGBBAA.
struct GraphicsVertex {
    float x{};
    float y{};
    float z{};
    std::uint32_t rgba{0xFFFFFFFFU};
    float u{};
    float v{};
};

struct GraphicsCapabilities {
    bool hardware_rasterization{};
    bool offscreen_gpu_upload{};
    bool indexed_geometry{};
    bool textures_and_samplers{};
    bool window_presentation{};
    std::string_view description{};
    bool native_shader_pipeline{};
};

// A frame is an off-screen RGBA8 color attachment plus a depth attachment.
// begin_frame opens the render pass; present_frame closes and publishes it.
class GraphicsBackend {
public:
    virtual ~GraphicsBackend() = default;

    [[nodiscard]] virtual bool initialize(std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool begin_frame(std::uint32_t width,
                                          std::uint32_t height,
                                          std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool clear(std::uint32_t rgba,
                                     std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool clear_depth(float depth,
                                           std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool draw_rect(const GraphicsRect& rect,
                                         std::uint32_t rgba,
                                         std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool create_texture(TextureId id,
                                              std::uint32_t width,
                                              std::uint32_t height,
                                              std::span<const std::uint32_t> pixels,
                                              const SamplerDesc& sampler = {},
                                              std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool destroy_texture(TextureId id,
                                               std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool draw_indexed(
        std::span<const GraphicsVertex> vertices,
        std::span<const std::uint32_t> indices,
        TextureId texture = 0U,
        std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool present_frame(std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool readback(std::span<std::uint32_t> pixels,
                                        std::string* error = nullptr) const = 0;

    [[nodiscard]] virtual GraphicsCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace lucid::gpu
