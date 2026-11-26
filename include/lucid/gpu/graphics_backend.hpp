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

struct GraphicsCapabilities {
    bool hardware_rasterization{};
    bool offscreen_gpu_upload{};
    std::string_view description{};
};

// Colors are packed as 0xRRGGBBAA. A frame is an off-screen RGBA8 image;
// window-system presentation is deliberately kept outside this interface.
class GraphicsBackend {
public:
    virtual ~GraphicsBackend() = default;

    [[nodiscard]] virtual bool initialize(std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool begin_frame(std::uint32_t width,
                                          std::uint32_t height,
                                          std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool clear(std::uint32_t rgba,
                                     std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool draw_rect(const GraphicsRect& rect,
                                         std::uint32_t rgba,
                                         std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool present_frame(std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool readback(std::span<std::uint32_t> pixels,
                                        std::string* error = nullptr) const = 0;

    [[nodiscard]] virtual GraphicsCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace lucid::gpu
