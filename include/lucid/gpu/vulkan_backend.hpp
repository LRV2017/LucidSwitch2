#pragma once

#include "lucid/gpu/graphics_backend.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace lucid::gpu {

// Native Vulkan renderer. When the Vulkan SDK and glslc are available this
// class owns the graphics pipeline, GPU buffers, sampled images, off-screen
// color/depth targets, readback, and (with SDL2) an optional Vulkan swapchain.
// Builds without those optional tools keep the portable software renderer.
class VulkanBackend final : public GraphicsBackend {
public:
    VulkanBackend();
    ~VulkanBackend() override;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    VulkanBackend(VulkanBackend&&) noexcept;
    VulkanBackend& operator=(VulkanBackend&&) noexcept;

    [[nodiscard]] static bool compiled_with_vulkan() noexcept;
    [[nodiscard]] static bool compiled_with_native_pipeline() noexcept;
    [[nodiscard]] static bool compiled_with_vulkan_window() noexcept;

    [[nodiscard]] bool initialize(std::string* error = nullptr) override;
    [[nodiscard]] bool initialize_window(std::string_view title,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         std::string* error = nullptr);
    [[nodiscard]] void poll_window_events() noexcept;
    [[nodiscard]] bool window_open() const noexcept;

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lucid::gpu
