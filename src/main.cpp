#include "lucid/core/emulator.hpp"
#include "lucid/gpu/vulkan_backend.hpp"
#include "lucid/gpu/software_backend.hpp"
#include "lucid/gpu/window_presenter.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
namespace {
constexpr const char* kVersion = "0.9.0";
constexpr std::uint64_t kDemoBase = 0x1000U;
constexpr std::uint64_t kDemoInstructionBudget = 16U;
void print_help() {
    std::cout
        << "LucidSwitch2 " << kVersion << "\n"
        << "Clean-room console emulation research scaffold\n\n"
        << "Usage:\n"
        << "  lucidswitch2 --help       Show this help\n"
        << "  lucidswitch2 --version    Show version\n"
        << "  lucidswitch2 --self-test  Run memory and CPU smoke checks\n"
        << "  lucidswitch2 --run        Execute a bounded synthetic A64 demo\n"
        << "  lucidswitch2 --gpu-info   Check optional Vulkan device support\n"
        << "  lucidswitch2 --graphics-demo [--window]  Render an original test scene\n\n"
        << "Status: no console hardware emulation or commercial game loading.\n";
}
int run_gpu_info() {
    lucid::gpu::VulkanBackend backend;
    if (!lucid::gpu::VulkanBackend::compiled_with_vulkan()) {
        std::cout << "Vulkan SDK: not detected at configure time\n"
                  << "Hardware rasterization: not implemented\n"
                  << "Software indexed renderer: available\n"
                  << "SDL2 window presenter: "
                  << (lucid::gpu::WindowPresenter::compiled_with_sdl2()
                        ? "available" : "not compiled") << "\n"
                  << "Install the Vulkan SDK and reconfigure to enable optional off-screen uploads.\n";
        return 0;
    }

    std::string error;
    if (!backend.initialize(&error)) {
        std::cerr << "Vulkan initialization failed: " << error << "\n";
        return 1;
    }

    const auto capabilities = backend.capabilities();
    std::cout << "Vulkan device: " << backend.name() << "\n"
              << "Off-screen GPU image upload: "
              << (capabilities.offscreen_gpu_upload ? "available" : "unavailable") << "\n"
              << "Hardware rasterization: "
              << (capabilities.hardware_rasterization ? "available" : "not implemented") << "\n"
              << "Indexed geometry and textures: "
              << (capabilities.indexed_geometry && capabilities.textures_and_samplers
                    ? "available through the reference renderer" : "unavailable") << "\n"
              << "SDL2 window presenter: "
              << (lucid::gpu::WindowPresenter::compiled_with_sdl2()
                    ? "available" : "not compiled") << "\n";
    return 0;
}

int run_graphics_demo(const bool use_window) {
    using namespace lucid::gpu;
    SoftwareBackend renderer;
    std::string error;
    if (!renderer.initialize(&error) ||
        !renderer.begin_frame(320U, 180U, &error) ||
        !renderer.clear(0x0B1020FFU, &error) ||
        !renderer.clear_depth(1.0F, &error)) {
        std::cerr << "Graphics demo setup failed: " << error << "\n";
        return 1;
    }

    constexpr std::array<std::uint32_t, 4> texels{
        0x26D7FFFFU, 0xFF4FA3FFU, 0x7A5CFFFFU, 0x42E6B9FFU
    };
    const SamplerDesc sampler{
        TextureFilter::Nearest, TextureFilter::Nearest,
        TextureAddress::Repeat, TextureAddress::Repeat
    };
    if (!renderer.create_texture(1U, 2U, 2U, texels, sampler, &error)) {
        std::cerr << "Texture setup failed: " << error << "\n";
        return 1;
    }

    const std::array<GraphicsVertex, 4> vertices{{
        {48.0F, 28.0F, 0.35F, 0xFFFFFFFFU, 0.0F, 0.0F},
        {272.0F, 28.0F, 0.35F, 0xFFFFFFFFU, 4.0F, 0.0F},
        {272.0F, 152.0F, 0.35F, 0xFFFFFFFFU, 4.0F, 3.0F},
        {48.0F, 152.0F, 0.35F, 0xFFFFFFFFU, 0.0F, 3.0F}
    }};
    constexpr std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 0U, 2U, 3U};
    if (!renderer.draw_indexed(vertices, indices, 1U, &error) ||
        !renderer.present_frame(&error)) {
        std::cerr << "Graphics demo rendering failed: " << error << "\n";
        return 1;
    }

    std::vector<std::uint32_t> pixels(320U * 180U);
    if (!renderer.readback(pixels, &error)) {
        std::cerr << "Graphics demo readback failed: " << error << "\n";
        return 1;
    }
    std::cout << "[PASS] RGBA8 color and depth framebuffer\n"
              << "[PASS] indexed vertex and index buffers\n"
              << "[PASS] sampled 2x2 texture with repeat sampler\n"
              << "[PASS] software triangle rasterization and framebuffer readback\n";

    if (!use_window) {
        std::cout << "Rendered a 320x180 original test scene. Use --window to open the desktop preview.\n";
        return 0;
    }

    WindowPresenter window;
    if (!window.initialize("LucidSwitch2 Graphics Demo", 960U, 540U, &error)) {
        std::cerr << "Window preview unavailable: " << error << "\n";
        return 1;
    }
    while (!window.should_close()) {
        if (!window.present(pixels, 320U, 180U, &error)) {
            std::cerr << "Window presentation failed: " << error << "\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

int run_synthetic_demo(bool self_test) {
    lucid::core::Emulator emulator;
    std::string error;
    const lucid::core::EmulatorConfig config{kDemoBase, 0x1000U, kDemoBase};
    if (!emulator.initialize(config, &error)) {
        std::cerr << "Initialization failed: " << error << "\n";
        return 1;
    }
    // B +0: a deterministic loop to exercise bounded execution and branching.
    constexpr std::array<std::uint8_t, 4U> branch_to_self{0x00U, 0x00U, 0x00U, 0x14U};
    if (!emulator.load_program(kDemoBase, branch_to_self, &error)) {
        std::cerr << "Program loading failed: " << error << "\n";
        return 1;
    }
    const auto result = emulator.run(kDemoInstructionBudget);
    const bool passed =
        result == lucid::core::RunResult::InstructionLimitReached &&
        emulator.cpu_context().instructions_retired == kDemoInstructionBudget &&
        emulator.cpu_context().program_counter == kDemoBase &&
        emulator.state() == lucid::core::SessionState::Paused;
    if (!passed) {
        std::cerr << "Synthetic CPU demo failed: "
                  << lucid::core::to_string(result) << ", CPU step: "
                  << lucid::core::to_string(emulator.last_step_result()) << "\n";
        return 1;
    }
    std::cout << "[PASS] memory mapping and executable instruction fetch\n"
              << "[PASS] A64 branch execution\n"
              << "[PASS] bounded CPU execution and pause state\n"
              << "Retired instructions: "
              << emulator.cpu_context().instructions_retired << "\n"
              << (self_test
                    ? "Foundation self-test passed. No console compatibility is implied.\n"
                    : "Synthetic CPU demo completed. No game was loaded.\n");
    return 0;
}
}
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    const std::string command = argv[1];
    if (command == "--help" || command == "-h") {
        print_help();
        return 0;
    }
    if (command == "--version") {
        std::cout << "LucidSwitch2 " << kVersion << "\n";
        return 0;
    }
    if (command == "--self-test") return run_synthetic_demo(true);
    if (command == "--run") return run_synthetic_demo(false);
    if (command == "--gpu-info") return run_gpu_info();
    if (command == "--graphics-demo")
        return run_graphics_demo(argc >= 3 && std::string(argv[2]) == "--window");
    std::cerr << "Unknown option: " << command << "\n\n";
    print_help();
    return 1;
}
