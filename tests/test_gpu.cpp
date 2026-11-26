#include "lucid/gpu/command_processor.hpp"
#include "lucid/gpu/guest_memory.hpp"
#include "lucid/gpu/software_backend.hpp"
#include "lucid/gpu/vulkan_backend.hpp"

#include <array>
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

void test_vulkan_build_configuration() {
    const auto capabilities = lucid::gpu::VulkanBackend{}.capabilities();
    if (lucid::gpu::VulkanBackend::compiled_with_vulkan()) {
        expect(capabilities.offscreen_gpu_upload,
               "Vulkan-enabled build advertises off-screen GPU upload");
    } else {
        expect(!capabilities.offscreen_gpu_upload,
               "build without Vulkan SDK reports unavailable GPU upload");
    }
}
} // namespace

int main() {
    test_guest_memory();
    test_command_processor_and_software_renderer();
    test_invalid_streams_are_rejected_before_execution();
    test_vulkan_build_configuration();

    if (failures != 0) {
        std::cerr << failures << " GPU test(s) failed.\n";
        return 1;
    }
    std::cout << "All GPU foundation tests passed.\n";
    return 0;
}
