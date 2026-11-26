#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace lucid::gpu {

// Optional SDL2 window presenter for displaying completed RGBA8 frames.
// It intentionally consumes renderer readback; it is not a Vulkan swapchain.
class WindowPresenter final {
public:
    WindowPresenter();
    ~WindowPresenter();

    WindowPresenter(const WindowPresenter&) = delete;
    WindowPresenter& operator=(const WindowPresenter&) = delete;
    WindowPresenter(WindowPresenter&&) noexcept;
    WindowPresenter& operator=(WindowPresenter&&) noexcept;

    [[nodiscard]] static bool compiled_with_sdl2() noexcept;
    [[nodiscard]] bool initialize(std::string_view title,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  std::string* error = nullptr);
    [[nodiscard]] bool present(std::span<const std::uint32_t> rgba_pixels,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::string* error = nullptr);
    void poll_events() noexcept;
    [[nodiscard]] bool should_close() const noexcept;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lucid::gpu
