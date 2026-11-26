#pragma once

#include "lucid/gpu/graphics_backend.hpp"
#include "lucid/gpu/software_backend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace lucid::gpu {

// Optional off-screen Vulkan upload backend. The current command stream is
// rasterized by SoftwareBackend and uploaded to a Vulkan device-local image;
// this establishes device/queue/resource plumbing without claiming a native
// GPU shader pipeline or window-system swapchain.
class VulkanBackend final : public GraphicsBackend {
public:
    VulkanBackend();
    ~VulkanBackend() override;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    VulkanBackend(VulkanBackend&&) noexcept;
    VulkanBackend& operator=(VulkanBackend&&) noexcept;

    [[nodiscard]] static bool compiled_with_vulkan() noexcept;

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SoftwareBackend reference_renderer_{};
    bool initialized_{};
};

} // namespace lucid::gpu
