#pragma once

#include "lucid/gpu/graphics_backend.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lucid::gpu {

class SoftwareBackend final : public GraphicsBackend {
public:
    [[nodiscard]] bool initialize(std::string* error = nullptr) override;
    [[nodiscard]] bool begin_frame(std::uint32_t width, std::uint32_t height,
                                   std::string* error = nullptr) override;
    [[nodiscard]] bool clear(std::uint32_t rgba,
                             std::string* error = nullptr) override;
    [[nodiscard]] bool draw_rect(const GraphicsRect& rect, std::uint32_t rgba,
                                 std::string* error = nullptr) override;
    [[nodiscard]] bool present_frame(std::string* error = nullptr) override;
    [[nodiscard]] bool readback(std::span<std::uint32_t> pixels,
                                std::string* error = nullptr) const override;

    [[nodiscard]] GraphicsCapabilities capabilities() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::uint64_t presented_frames() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::uint32_t> pixels_{};
    std::uint64_t presented_frames_{};
    bool initialized_{};
    bool frame_open_{};
    bool frame_presented_{};
};

} // namespace lucid::gpu
