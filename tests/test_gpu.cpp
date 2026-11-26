#include "lucid/gpu/command_processor.hpp"
#include "lucid/gpu/guest_memory.hpp"
#include "lucid/gpu/software_backend.hpp"
#include "lucid/gpu/vulkan_backend.hpp"
#include "lucid/gpu/window_presenter.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(const bool condition, const char* message) {
    if (condition) std::cout << "[PASS] " << message << '\n';
    else {
        ++failures;
        std::cerr << "[FAIL] " << message << '\n';
    }
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void packet(std::vector<std::uint8_t>& bytes, lucid::gpu::GpuOpcode opcode,
            std::initializer_list<std::uint32_t> payload = {}) {
    append_u32(bytes, lucid::gpu::encode_header(opcode,
        static_cast<std::uint32_t>(payload.size())));
    for (const auto word : payload) append_u32(bytes, word);
}

void test_guest_memory() {
    lucid::gpu::GuestGpuMemory memory(0x800000U, 64U);
    const std::array<std::uint8_t, 4> source{1U, 2U, 3U, 4U};
    std::array<std::uint8_t, 4> target{};
    expect(memory.valid(), "create bounded guest GPU memory");
    expect(memory.write(0x800010U, source), "write guest GPU memory");
    expect(memory.read(0x800010U, target) && target == source,
           "read guest GPU memory");
    expect(!memory.read(0x80003FU, target),
           "reject guest GPU memory read beyond range");
    expect(!memory.write(0x7FFFFFU, source),
           "reject guest GPU memory write below base");
    expect(memory.fill(0x800010U, 4U, 0xA5U) &&
           memory.read(0x800010U, target) && target[0] == 0xA5U &&
           target[3] == 0xA5U, "fill guest GPU memory range");
}

void test_command_processor_and_software_renderer() {
    using namespace lucid::gpu;
    GuestGpuMemory memory(0x100000U, 4096U);
    std::vector<std::uint8_t> stream;
    packet(stream, GpuOpcode::BeginFrame, {4U, 3U});
    packet(stream, GpuOpcode::Clear, {0x101010FFU});
    packet(stream, GpuOpcode::DrawRect, {1U, 1U, 2U, 2U, 0xFF0000FFU});
    packet(stream, GpuOpcode::Present);
    packet(stream, GpuOpcode::End);
    expect(memory.write(0x100000U, stream), "write guest GPU command stream");

    SoftwareBackend renderer;
    std::string error;
    expect(renderer.initialize(&error), "initialize software reference renderer");
    GpuCommandProcessor processor;
    expect(processor.submit(memory, 0x100000U, stream.size(), renderer, &error),
           "submit validated guest GPU command buffer");
    expect(processor.stats().submissions == 1U &&
           processor.stats().frames_presented == 1U &&
           processor.stats().commands == 5U,
           "record GPU processor submission statistics");

    std::array<std::uint32_t, 12> pixels{};
    expect(renderer.readback(pixels, &error), "read back software frame");
    expect(pixels[0] == 0x101010FFU && pixels[5] == 0xFF0000FFU &&
           pixels[6] == 0xFF0000FFU && pixels[9] == 0xFF0000FFU,
           "clear and rectangle draw produce expected RGBA pixels");
    expect(renderer.presented_frames() == 1U && renderer.width() == 4U &&
           renderer.height() == 3U, "track software frame extent and count");
}

void test_invalid_streams_are_rejected_before_execution() {
    using namespace lucid::gpu;
    GuestGpuMemory memory(0x200000U, 256U);
    SoftwareBackend renderer;
    std::string error;
    expect(renderer.initialize(&error), "initialize renderer for validation tests");
    GpuCommandProcessor processor;

    std::vector<std::uint8_t> invalid;
    packet(invalid, GpuOpcode::BeginFrame, {4U, 4U});
    packet(invalid, GpuOpcode::DrawRect, {3U, 3U, 2U, 2U, 0xFFFFFFFFU});
    packet(invalid, GpuOpcode::Present);
    packet(invalid, GpuOpcode::End);
    expect(memory.write(0x200000U, invalid), "write invalid rectangle command stream");
    expect(!processor.submit(memory, 0x200000U, invalid.size(), renderer, &error),
           "reject rectangle outside current render target");
    expect(processor.stats().submissions == 0U && renderer.presented_frames() == 0U,
           "invalid stream does not execute any backend commands");

    invalid.clear();
    packet(invalid, static_cast<GpuOpcode>(0x7EU));
    packet(invalid, GpuOpcode::End);
    expect(memory.write(0x200000U, invalid), "write unknown opcode stream");
    expect(!processor.submit(memory, 0x200000U, invalid.size(), renderer, &error),
           "reject unknown GPU opcode");

    invalid.clear();
    packet(invalid, GpuOpcode::BeginFrame, {4U, 4U});
    packet(invalid, GpuOpcode::Clear, {0U});
    expect(memory.write(0x200000U, invalid), "write stream missing END");
    expect(!processor.submit(memory, 0x200000U, invalid.size(), renderer, &error),
           "reject command buffer missing END");

    expect(!processor.submit(memory, 0x200000U, 3U, renderer, &error),
           "reject unaligned command buffer length");
    expect(!processor.submit(memory, 0x2000FFU, 8U, renderer, &error),
           "reject command buffer outside guest memory");
}

void test_indexed_geometry_texture_and_depth() {
    using namespace lucid::gpu;
    SoftwareBackend renderer;
    std::string error;
    expect(renderer.initialize(&error), "initialize indexed reference renderer");
    expect(renderer.begin_frame(4U, 4U, &error), "begin color/depth render pass");
    expect(renderer.clear(0x000000FFU, &error), "clear color attachment");
    expect(renderer.clear_depth(1.0F, &error), "clear depth attachment");

    const std::array<std::uint32_t, 1> texels{0x00FF00FFU};
    expect(renderer.create_texture(7U, 1U, 1U, texels, {}, &error),
           "create sampled texture with nearest sampler");
    const std::array<GraphicsVertex, 3> triangle{{
        {0.0F, 0.0F, 0.5F, 0xFFFFFFFFU, 0.5F, 0.5F},
        {4.0F, 0.0F, 0.5F, 0xFFFFFFFFU, 0.5F, 0.5F},
        {0.0F, 4.0F, 0.5F, 0xFFFFFFFFU, 0.5F, 0.5F}
    }};
    constexpr std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
    expect(renderer.draw_indexed(triangle, indices, 7U, &error),
           "draw textured indexed triangle");
    auto behind = triangle;
    for (auto& vertex : behind) {
        vertex.z = 0.8F;
        vertex.rgba = 0xFF0000FFU;
    }
    expect(renderer.draw_indexed(behind, indices, 0U, &error),
           "draw second indexed triangle behind first");
    expect(renderer.present_frame(&error), "end render pass and publish framebuffer");

    std::array<std::uint32_t, 16> pixels{};
    expect(renderer.readback(pixels, &error), "read back color attachment");
    expect(pixels[0] == 0x00FF00FFU,
           "nearest texture sampling shades covered triangle pixels");
    expect(pixels[15] == 0x000000FFU,
           "pixels outside triangle retain framebuffer clear color");
    expect(renderer.texture_count() == 1U,
           "texture remains resident after frame presentation");
    expect(renderer.destroy_texture(7U, &error) && renderer.texture_count() == 0U,
           "destroy texture resource");
    expect(!renderer.destroy_texture(7U, &error),
           "reject double texture destruction");

    const std::array<std::uint32_t, 2> linear_texels{
        0xFF0000FFU, 0x0000FFFFU
    };
    const SamplerDesc linear_sampler{
        TextureFilter::Linear, TextureFilter::Linear,
        TextureAddress::Clamp, TextureAddress::Clamp
    };
    expect(renderer.create_texture(8U, 2U, 1U, linear_texels,
                                   linear_sampler, &error),
           "create linear-filtered texture");
    expect(renderer.begin_frame(4U, 4U, &error),
           "begin frame for bilinear sampler test");
    expect(renderer.clear(0x000000FFU, &error) &&
           renderer.clear_depth(1.0F, &error),
           "clear color and depth before bilinear sampler test");
    auto linear_triangle = triangle;
    for (auto& vertex : linear_triangle) vertex.u = 0.5F;
    expect(renderer.draw_indexed(linear_triangle, indices, 8U, &error),
           "draw triangle with linear-filtered texture");
    expect(renderer.present_frame(&error), "present bilinear sampler test frame");
    expect(renderer.readback(pixels, &error) && pixels[0] == 0x800080FFU,
           "linear sampler blends adjacent texels");
    expect(renderer.destroy_texture(8U, &error),
           "destroy linear-filtered texture");

    const std::array<std::uint32_t, 1> one_pixel{0xFFFFFFFFU};
    expect(renderer.create_texture(8U, 1U, 1U, one_pixel, {}, &error),
           "reuse a texture ID after destruction");
    expect(!renderer.create_texture(8U, 1U, 1U, one_pixel, {}, &error),
           "reject duplicate texture IDs");
    expect(renderer.begin_frame(4U, 4U, &error),
           "begin a second frame for invalid binding test");
    expect(!renderer.draw_indexed(triangle, indices, 99U, &error),
           "reject unknown texture binding");

}

void append_words(std::vector<std::uint8_t>& bytes,
                  const std::initializer_list<std::uint32_t> words) {
    for (const auto word : words) append_u32(bytes, word);
}

void test_guest_indexed_command_stream() {
    using namespace lucid::gpu;
    constexpr std::uint64_t base = 0x300000U;
    constexpr std::uint64_t command_address = base;
    constexpr std::uint64_t vertex_address = base + 0x1000U;
    constexpr std::uint64_t index_address = base + 0x1200U;
    constexpr std::uint64_t texture_address = base + 0x1300U;
    GuestGpuMemory memory(base, 8192U);
    std::string error;

    const std::array<std::uint32_t, 1> texture_pixel{0x0000FFFFU};
    std::vector<std::uint8_t> texture_bytes;
    append_words(texture_bytes, {texture_pixel[0]});
    expect(memory.write(texture_address, texture_bytes),
           "write guest texture texel");

    const auto f = [](const float value) { return std::bit_cast<std::uint32_t>(value); };
    std::vector<std::uint8_t> vertex_bytes;
    append_words(vertex_bytes, {
        f(0.0F), f(0.0F), f(0.3F), 0xFFFFFFFFU, f(0.5F), f(0.5F),
        f(4.0F), f(0.0F), f(0.3F), 0xFFFFFFFFU, f(0.5F), f(0.5F),
        f(0.0F), f(4.0F), f(0.3F), 0xFFFFFFFFU, f(0.5F), f(0.5F)
    });
    expect(memory.write(vertex_address, vertex_bytes),
           "write guest vertex buffer");

    std::vector<std::uint8_t> index_bytes;
    append_words(index_bytes, {0U, 1U, 2U});
    expect(memory.write(index_address, index_bytes),
           "write guest index buffer");

    std::vector<std::uint8_t> stream;
    packet(stream, GpuOpcode::CreateTexture,
           {1U, 1U, 1U, static_cast<std::uint32_t>(texture_address),
            static_cast<std::uint32_t>(texture_address >> 32U), 0U});
    packet(stream, GpuOpcode::BeginFrame, {4U, 4U});
    packet(stream, GpuOpcode::Clear, {0x101010FFU});
    packet(stream, GpuOpcode::ClearDepth, {std::bit_cast<std::uint32_t>(1.0F)});
    packet(stream, GpuOpcode::DrawIndexed,
           {static_cast<std::uint32_t>(vertex_address),
            static_cast<std::uint32_t>(vertex_address >> 32U), 3U,
            static_cast<std::uint32_t>(index_address),
            static_cast<std::uint32_t>(index_address >> 32U), 3U, 1U});
    packet(stream, GpuOpcode::Present);
    packet(stream, GpuOpcode::DestroyTexture, {1U});
    packet(stream, GpuOpcode::End);
    expect(memory.write(command_address, stream),
           "write guest indexed GPU command buffer");

    SoftwareBackend renderer;
    expect(renderer.initialize(&error), "initialize command-stream renderer");
    GpuCommandProcessor processor;
    expect(processor.submit(memory, command_address, stream.size(), renderer, &error),
           "execute texture and indexed-draw GPU commands");
    expect(processor.stats().indexed_draws == 1U &&
           processor.stats().textures_created == 1U &&
           processor.stats().textures_destroyed == 1U &&
           processor.live_texture_count() == 0U,
           "track indexed draw and guest texture lifetime");

    std::array<std::uint32_t, 16> pixels{};
    expect(renderer.readback(pixels, &error) && pixels[0] == 0x0000FFFFU,
           "guest command stream produces textured triangle pixels");
}

void test_window_presenter_configuration() {
    const bool available = lucid::gpu::WindowPresenter::compiled_with_sdl2();
    lucid::gpu::WindowPresenter presenter;
    std::string error;
    if (!available) {
        expect(!presenter.initialize("LucidSwitch2 test", 64U, 64U, &error),
               "report missing optional SDL2 dependency");
    } else {
        expect(!presenter.should_close(),
               "SDL2 presenter starts in the open state");
    }
}

void test_vulkan_build_configuration() {
    using lucid::gpu::VulkanBackend;
    const auto capabilities = VulkanBackend{}.capabilities();
    if (VulkanBackend::compiled_with_native_pipeline()) {
        expect(capabilities.native_shader_pipeline,
               "native Vulkan build advertises the shader pipeline");
        expect(capabilities.indexed_geometry && capabilities.textures_and_samplers,
               "native Vulkan build advertises GPU geometry and texture support");
    } else {
        expect(!capabilities.native_shader_pipeline,
               "portable build reports that native Vulkan shaders are unavailable");
    }
    if (VulkanBackend::compiled_with_native_pipeline()) {
        expect(capabilities.offscreen_gpu_upload,
               "native Vulkan build advertises off-screen GPU rendering");
    } else {
        expect(!capabilities.offscreen_gpu_upload,
               "build without compiled shaders reports unavailable native GPU rendering");
    }
}

void test_native_vulkan_rendering() {
    using namespace lucid::gpu;
    if (!VulkanBackend::compiled_with_native_pipeline()) return;

    VulkanBackend renderer;
    std::string error;
    const bool initialized = renderer.initialize(&error);
    expect(initialized, "initialize native Vulkan renderer and select a graphics device");
    if (!initialized) {
        std::cerr << "[VULKAN] " << error << "\n";
        return;
    }

    const std::array<std::uint32_t, 1U> blue_texel{0x0000FFFFU};
    const SamplerDesc sampler{
        TextureFilter::Nearest, TextureFilter::Nearest,
        TextureAddress::Clamp, TextureAddress::Clamp
    };
    expect(renderer.create_texture(31U, 1U, 1U, blue_texel, sampler, &error),
           "upload a sampled texture image to Vulkan device memory");

    expect(renderer.begin_frame(8U, 8U, &error),
           "begin native Vulkan color/depth render pass");
    expect(renderer.clear(0x101010FFU, &error) &&
           renderer.clear_depth(1.0F, &error),
           "clear native Vulkan color and depth attachments");

    const std::array<GraphicsVertex, 4U> blue_quad{{
        {1.0F, 1.0F, 0.2F, 0xFFFFFFFFU, 0.5F, 0.5F},
        {7.0F, 1.0F, 0.2F, 0xFFFFFFFFU, 0.5F, 0.5F},
        {7.0F, 7.0F, 0.2F, 0xFFFFFFFFU, 0.5F, 0.5F},
        {1.0F, 7.0F, 0.2F, 0xFFFFFFFFU, 0.5F, 0.5F}
    }};
    constexpr std::array<std::uint32_t, 6U> quad_indices{
        0U, 1U, 2U, 0U, 2U, 3U
    };
    expect(renderer.draw_indexed(blue_quad, quad_indices, 31U, &error),
           "submit indexed GPU geometry with a sampled texture");

    auto occluded_red_quad = blue_quad;
    for (auto& vertex : occluded_red_quad) {
        vertex.z = 0.8F;
        vertex.rgba = 0xFF0000FFU;
    }
    expect(renderer.draw_indexed(occluded_red_quad, quad_indices, 0U, &error),
           "submit farther untextured geometry for Vulkan depth testing");
    expect(renderer.present_frame(&error),
           "execute Vulkan command buffer and publish the rendered image");

    std::array<std::uint32_t, 64U> pixels{};
    expect(renderer.readback(pixels, &error),
           "read back the completed native Vulkan framebuffer");
    expect(pixels[4U * 8U + 4U] == 0x0000FFFFU,
           "GPU fragment shader samples blue texture and depth rejects farther red quad");
    expect(pixels[0] == 0x101010FFU,
           "native Vulkan readback preserves clear color outside the geometry");

    expect(renderer.destroy_texture(31U, &error),
           "release native Vulkan texture, sampler, and descriptor resources");
}

} // namespace

int main() {
    test_guest_memory();
    test_command_processor_and_software_renderer();
    test_invalid_streams_are_rejected_before_execution();
    test_indexed_geometry_texture_and_depth();
    test_guest_indexed_command_stream();
    test_window_presenter_configuration();
    test_vulkan_build_configuration();
    test_native_vulkan_rendering();

    if (failures != 0) {
        std::cerr << failures << " GPU test(s) failed.\n";
        return 1;
    }
    std::cout << "All GPU foundation tests passed.\n";
    return 0;
}
