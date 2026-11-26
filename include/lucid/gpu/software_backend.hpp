#pragma once

#include "lucid/gpu/graphics_backend.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lucid::gpu {

class SoftwareBackend final : public GraphicsBackend {
public:
    [[nodiscard]] bool initialize(std::string* error = nullptr) override;
    [[nodiscard]] bool begin_frame(std::uint32_t width, std::uint32_t height,
                                   std::string* error = nullptr) override;
    [[nodiscard]] bool clear(std::uint32_t rgba,
                             std::string* error = nullptr) override;
    [[nodiscard]] bool clear_depth(float depth,
                                   std::string* error = nullptr) override;
    [[nodiscard]] bool draw_rect(const GraphicsRect& rect, std::uint32_t rgba,
                                 std::string* error = nullptr) override;
    [[nodiscard]] bool create_texture(TextureId id, std::uint32_t width,
                                      std::uint32_t height,
                                      std::span<const std::uint32_t> pixels,
                                      const SamplerDesc& sampler = {},
                                      std::string* error = nullptr) override;
    [[nodiscard]] bool destroy_texture(TextureId id,
                                       std::string* error = nullptr) override;
    [[nodiscard]] bool draw_indexed(std::span<const GraphicsVertex> vertices,
                                    std::span<const std::uint32_t> indices,
                                    TextureId texture = 0U,
                                    std::string* error = nullptr) override;
    [[nodiscard]] bool present_frame(std::string* error = nullptr) override;
    [[nodiscard]] bool readback(std::span<std::uint32_t> pixels,
                                std::string* error = nullptr) const override;

    [[nodiscard]] GraphicsCapabilities capabilities() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::uint64_t presented_frames() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::size_t texture_count() const noexcept;

private:
    struct Texture {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint32_t> pixels{};
        SamplerDesc sampler{};
    };

    [[nodiscard]] std::uint32_t sample_texture(const Texture& texture,
                                               float u, float v) const noexcept;
    [[nodiscard]] bool rasterize_triangle(const GraphicsVertex& a,
                                          const GraphicsVertex& b,
                                          const GraphicsVertex& c,
                                          const Texture* texture,
                                          std::string* error);

    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::uint32_t> pixels_{};
    std::vector<float> depth_{};
    std::unordered_map<TextureId, Texture> textures_{};
    std::uint64_t presented_frames_{};
    bool initialized_{};
    bool frame_open_{};
    bool frame_presented_{};
};

} // namespace lucid::gpu
