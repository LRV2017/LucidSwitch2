#include "lucid/core/emulator.hpp"
#include "lucid/gpu/vulkan_backend.hpp"
#include "lucid/gpu/software_backend.hpp"
#include "lucid/gpu/window_presenter.hpp"
#include "lucid/homebrew/package.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr const char* kVersion = "0.10.0";
constexpr std::uint64_t kDemoBase = 0x1000U;
constexpr std::uint64_t kDemoInstructionBudget = 16U;
constexpr std::uint64_t kDefaultHomebrewBudget = 100000U;
constexpr std::uint64_t kMaximumHomebrewBudget = 10000000U;
constexpr std::uint64_t kLoaderReserve = 64U * 1024U;
constexpr std::uint64_t kPageSize = 4096U;
constexpr std::uint64_t kLowerAddressLimit = 1ULL << 48U;

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
        << "  lucidswitch2 --graphics-demo [--window]  Render an original test scene\n"
        << "  lucidswitch2 --create-homebrew-sample <output.lxp>\n"
        << "  lucidswitch2 --inspect-homebrew <program.lxp>\n"
        << "  lucidswitch2 --run-homebrew <program.lxp> [instruction-budget]\n\n"
        << "LXP is an original synthetic test format. No commercial console games are loaded.\n";
}

bool parse_instruction_budget(const char* text, std::uint64_t& budget) {
    if (text == nullptr || *text == '\0') return false;
    const char* end = text;
    while (*end != '\0') ++end;
    const auto result = std::from_chars(text, end, budget);
    return result.ec == std::errc{} && result.ptr == end &&
           budget > 0U && budget <= kMaximumHomebrewBudget;
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

int run_synthetic_demo(const bool self_test) {
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

int create_homebrew_sample(const std::filesystem::path& path) {
    lucid::homebrew::ProgramPackage program;
    // MOVZ X0, #42; MOVZ X1, #0xC0DE; B . in little-endian A64 form.
    program.code = {
        0x40U, 0x05U, 0x80U, 0xD2U,
        0xC1U, 0x1BU, 0x98U, 0xD2U,
        0x00U, 0x00U, 0x00U, 0x14U
    };
    program.bss_size = 256U;
    program.stack_size = 64U * 1024U;

    std::string error;
    if (!lucid::homebrew::write_program_file(path, program, &error)) {
        std::cerr << "Could not create homebrew sample: " << error << "\n";
        return 1;
    }
    std::cout << "Created original LXP sample: " << path.string() << "\n"
              << "Code bytes: " << program.code.size() << "\n"
              << "Entry offset: 0x" << std::hex << program.entry_offset << std::dec << "\n"
              << "Run with: lucidswitch2 --run-homebrew <file> 3\n";
    return 0;
}

int inspect_homebrew(const std::filesystem::path& path) {
    lucid::homebrew::ProgramPackage program;
    std::string error;
    if (!lucid::homebrew::read_program_file(path, program, &error)) {
        std::cerr << "LXP inspection failed: " << error << "\n";
        return 1;
    }
    std::cout << "LXP package version: " << lucid::homebrew::PackageVersion << "\n"
              << "Load address: 0x" << std::hex << program.load_address << std::dec << "\n"
              << "Entry offset: " << program.entry_offset << "\n"
              << "Code bytes: " << program.code.size() << "\n"
              << "Data bytes: " << program.data.size() << "\n"
              << "BSS bytes: " << program.bss_size << "\n"
              << "Stack bytes: " << program.stack_size << "\n"
              << "Payload checksum: valid (CRC-32)\n";
    return 0;
}

int run_homebrew(const std::filesystem::path& path,
                 const std::uint64_t instruction_budget) {
    lucid::homebrew::ProgramPackage program;
    std::string error;
    if (!lucid::homebrew::read_program_file(path, program, &error)) {
        std::cerr << "LXP load failed: " << error << "\n";
        return 1;
    }

    const std::uint64_t image_bytes =
        static_cast<std::uint64_t>(program.code.size()) +
        static_cast<std::uint64_t>(program.data.size());
    const std::uint64_t footprint = image_bytes +
        static_cast<std::uint64_t>(program.bss_size) +
        static_cast<std::uint64_t>(program.stack_size) + kLoaderReserve;
    if (footprint > std::numeric_limits<std::uint64_t>::max() -
                        (kPageSize - 1U)) {
        std::cerr << "LXP guest memory size overflows the host address type.\n";
        return 1;
    }
    const std::uint64_t memory_size =
        (footprint + (kPageSize - 1U)) & ~(kPageSize - 1U);
    if (memory_size > lucid::homebrew::MaximumGuestFootprint + kLoaderReserve ||
        memory_size > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()) ||
        memory_size > kLowerAddressLimit - program.load_address) {
        std::cerr << "LXP guest memory range exceeds the supported bounded address space.\n";
        return 1;
    }

    lucid::core::Emulator emulator;
    const lucid::core::EmulatorConfig config{
        program.load_address,
        static_cast<std::size_t>(memory_size),
        program.load_address + program.entry_offset
    };
    if (!emulator.initialize(config, &error)) {
        std::cerr << "Could not initialize the LXP guest session: " << error << "\n";
        return 1;
    }

    std::vector<std::uint8_t> image;
    image.reserve(static_cast<std::size_t>(image_bytes));
    image.insert(image.end(), program.code.begin(), program.code.end());
    image.insert(image.end(), program.data.begin(), program.data.end());
    if (!emulator.load_program(program.load_address, image, &error)) {
        std::cerr << "Could not load the LXP image into guest memory: " << error << "\n";
        return 1;
    }
    emulator.cpu_context().sp = program.load_address + memory_size;

    const auto result = emulator.run(instruction_budget);
    if (result != lucid::core::RunResult::InstructionLimitReached) {
        std::cerr << "LXP execution failed: " << lucid::core::to_string(result)
                  << " (last CPU step: "
                  << lucid::core::to_string(emulator.last_step_result()) << ")\n";
        return 1;
    }

    const auto& cpu = emulator.cpu_context();
    std::cout << "[PASS] LXP header, segment bounds, and CRC-32 validated\n"
              << "[PASS] guest memory initialized and program image loaded\n"
              << "[PASS] bounded A64 execution reached its instruction budget\n"
              << "Instructions retired: " << cpu.instructions_retired << "\n"
              << "Program counter: 0x" << std::hex << cpu.program_counter << "\n"
              << "X0: 0x" << cpu.x[0] << "\n"
              << "X1: 0x" << cpu.x[1] << "\n"
              << "Stack pointer: 0x" << cpu.sp << std::dec << "\n"
              << "Session state: " << lucid::core::to_string(emulator.state()) << "\n"
              << "The session is paused at the host instruction budget; no guest OS or console API is present.\n";
    return 0;
}
} // namespace

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
    if (command == "--create-homebrew-sample") {
        if (argc != 3) {
            std::cerr << "Usage: --create-homebrew-sample <output.lxp>\n";
            return 2;
        }
        return create_homebrew_sample(argv[2]);
    }
    if (command == "--inspect-homebrew") {
        if (argc != 3) {
            std::cerr << "Usage: --inspect-homebrew <program.lxp>\n";
            return 2;
        }
        return inspect_homebrew(argv[2]);
    }
    if (command == "--run-homebrew") {
        if (argc < 3 || argc > 4) {
            std::cerr << "Usage: --run-homebrew <program.lxp> [instruction-budget]\n";
            return 2;
        }
        std::uint64_t budget = kDefaultHomebrewBudget;
        if (argc == 4 && !parse_instruction_budget(argv[3], budget)) {
            std::cerr << "Instruction budget must be between 1 and "
                      << kMaximumHomebrewBudget << ".\n";
            return 2;
        }
        return run_homebrew(argv[2], budget);
    }
    std::cerr << "Unknown option: " << command << "\n\n";
    print_help();
    return 1;
}
