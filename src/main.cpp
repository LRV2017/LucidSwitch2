#include "lucid/core/emulator.hpp"
#include "lucid/gpu/vulkan_backend.hpp"
#include "lucid/gpu/software_backend.hpp"
#include "lucid/gpu/window_presenter.hpp"
#include "lucid/homebrew/package.hpp"
#include "lucid/switch/executable.hpp"
#include "lucid/switch/compatibility.hpp"
#include "lucid/switch/homebrew_loader.hpp"
#include "lucid/switch/nsp_package.hpp"
#include "lucid/switch2/cpu_cluster.hpp"

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
constexpr const char* kVersion = "0.22.0";
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
        << "  lucidswitch2 --switch2-cpu-demo  Run the Switch 2 eight-core CPU-context demo\n"
        << "  lucidswitch2 --graphics-demo [--window] [--vulkan]  Render an original test scene\n"
        << "  lucidswitch2 --create-homebrew-sample <output.lxp>\n"
        << "  lucidswitch2 --inspect-homebrew <program.lxp>\n"
        << "  lucidswitch2 --inspect-switch-executable <program.nro|program.nso>\n"
        << "  lucidswitch2 --compat-report <switch|switch2|all>\n"
        << "  lucidswitch2 --inspect-nsp <game.nsp>\n"
        << "  lucidswitch2 --run-nro <homebrew.nro> [instruction-budget]\n"
        << "  lucidswitch2 --run-homebrew <program.lxp> [instruction-budget]\n\n"
        << "NRO execution is restricted to minimal plaintext homebrew; no Horizon OS, imports, relocations, or commercial games.\n"
        << "Protected NSP/XCI/NCA content and production/title-key decryption are not supported.\n";
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
    using lucid::gpu::VulkanBackend;
    if (!VulkanBackend::compiled_with_native_pipeline()) {
        const auto capabilities = VulkanBackend{}.capabilities();
        std::cout << "Vulkan SDK: "
                  << (VulkanBackend::compiled_with_vulkan() ? "available" : "not detected")
                  << "\n"
                  << "Native shader pipeline: not compiled\n"
                  << "Software reference renderer: available\n"
                  << capabilities.description << "\n";
        return 0;
    }

    VulkanBackend backend;
    std::string error;
    if (!backend.initialize(&error)) {
        std::cerr << "Native Vulkan initialization failed: " << error << "\n";
        return 1;
    }
    const auto capabilities = backend.capabilities();
    std::cout << "Vulkan SDK: available\n"
              << "Selected Vulkan device: " << backend.name() << "\n"
              << "Native shader pipeline: available\n"
              << "GPU indexed geometry and textures: "
              << (capabilities.indexed_geometry && capabilities.textures_and_samplers
                    ? "available" : "unavailable") << "\n"
              << "Hardware-class device: "
              << (capabilities.hardware_rasterization
                    ? "discrete/integrated GPU" : "software or other Vulkan device") << "\n"
              << "Vulkan swapchain presentation: "
              << (VulkanBackend::compiled_with_vulkan_window()
                    ? "compiled (requires a compatible desktop surface)" : "not compiled")
              << "\n"
              << capabilities.description << "\n";
    return 0;
}

int render_scene(lucid::gpu::GraphicsBackend& renderer,
                 std::vector<std::uint32_t>& pixels,
                 std::string& error) {
    using namespace lucid::gpu;
    if (!renderer.begin_frame(320U, 180U, &error) ||
        !renderer.clear(0x0B1020FFU, &error) ||
        !renderer.clear_depth(1.0F, &error)) {
        return 1;
    }

    const std::array<GraphicsVertex, 4U> vertices{{
        {48.0F, 28.0F, 0.35F, 0xFFFFFFFFU, 0.0F, 0.0F},
        {272.0F, 28.0F, 0.35F, 0xFFFFFFFFU, 4.0F, 0.0F},
        {272.0F, 152.0F, 0.35F, 0xFFFFFFFFU, 4.0F, 3.0F},
        {48.0F, 152.0F, 0.35F, 0xFFFFFFFFU, 0.0F, 3.0F}
    }};
    constexpr std::array<std::uint32_t, 6U> indices{
        0U, 1U, 2U, 0U, 2U, 3U
    };
    if (!renderer.draw_indexed(vertices, indices, 1U, &error) ||
        !renderer.present_frame(&error)) return 1;

    if (!pixels.empty() && !renderer.readback(pixels, &error)) return 1;
    return 0;
}

int run_native_graphics_demo(const bool use_window) {
    using namespace lucid::gpu;
    VulkanBackend renderer;
    std::string error;
    const bool use_vulkan_window =
        use_window && VulkanBackend::compiled_with_vulkan_window();
    const bool initialized = use_vulkan_window
        ? renderer.initialize_window("LucidSwitch2 Native Vulkan Demo",
                                     960U, 540U, &error)
        : renderer.initialize(&error);
    if (!initialized) {
        std::cerr << "Native Vulkan setup failed: " << error << "\n";
        return 1;
    }

    constexpr std::array<std::uint32_t, 4U> texels{
        0x26D7FFFFU, 0xFF4FA3FFU, 0x7A5CFFFFU, 0x42E6B9FFU
    };
    const SamplerDesc sampler{
        TextureFilter::Nearest, TextureFilter::Nearest,
        TextureAddress::Repeat, TextureAddress::Repeat
    };
    if (!renderer.create_texture(1U, 2U, 2U, texels, sampler, &error)) {
        std::cerr << "Native Vulkan texture setup failed: " << error << "\n";
        return 1;
    }

    std::vector<std::uint32_t> pixels(use_vulkan_window ? 0U : 320U * 180U);
    if (use_vulkan_window) {
        while (renderer.window_open()) {
            renderer.poll_window_events();
            if (!renderer.window_open()) break;
            if (render_scene(renderer, pixels, error) != 0) {
                std::cerr << "Native Vulkan frame failed: " << error << "\n";
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    } else {
        if (render_scene(renderer, pixels, error) != 0) {
            std::cerr << "Native Vulkan rendering failed: " << error << "\n";
            return 1;
        }
        std::cout << "[PASS] native Vulkan vertex shader and triangle-list input\n"
                  << "[PASS] GPU vertex and index buffers\n"
                  << "[PASS] GPU texture image, sampler, and fragment shader\n"
                  << "[PASS] depth-tested Vulkan render pass\n"
                  << "[PASS] GPU framebuffer readback\n"
                  << "Vulkan device: " << renderer.name() << "\n";
    }
    (void)renderer.destroy_texture(1U, &error);
    return 0;
}

int run_software_graphics_demo(const bool use_window) {
    using namespace lucid::gpu;
    SoftwareBackend renderer;
    std::string error;
    if (!renderer.initialize(&error)) {
        std::cerr << "Software graphics setup failed: " << error << "\n";
        return 1;
    }
    constexpr std::array<std::uint32_t, 4U> texels{
        0x26D7FFFFU, 0xFF4FA3FFU, 0x7A5CFFFFU, 0x42E6B9FFU
    };
    const SamplerDesc sampler{
        TextureFilter::Nearest, TextureFilter::Nearest,
        TextureAddress::Repeat, TextureAddress::Repeat
    };
    if (!renderer.create_texture(1U, 2U, 2U, texels, sampler, &error)) {
        std::cerr << "Software texture setup failed: " << error << "\n";
        return 1;
    }
    std::vector<std::uint32_t> pixels(320U * 180U);
    if (render_scene(renderer, pixels, error) != 0) {
        std::cerr << "Software graphics rendering failed: " << error << "\n";
        return 1;
    }
    std::cout << "[PASS] CPU reference triangle rasterization\n"
              << "[PASS] textured framebuffer readback\n";
    if (!use_window) return 0;

    WindowPresenter window;
    if (!window.initialize("LucidSwitch2 Software Graphics Preview",
                           960U, 540U, &error)) {
        std::cerr << "Software window preview unavailable: " << error << "\n";
        return 1;
    }
    while (!window.should_close()) {
        if (!window.present(pixels, 320U, 180U, &error)) {
            std::cerr << "Software window presentation failed: " << error << "\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

int run_graphics_demo(const bool use_window, const bool force_vulkan) {
    using lucid::gpu::VulkanBackend;
    if (VulkanBackend::compiled_with_native_pipeline()) {
        if (use_window && !VulkanBackend::compiled_with_vulkan_window()) {
            if (force_vulkan) {
                std::cerr << "Vulkan swapchain support was not compiled; install SDL2 and rebuild.\n";
                return 1;
            }
            return run_software_graphics_demo(true);
        }
        return run_native_graphics_demo(use_window);
    }
    if (force_vulkan) {
        std::cerr << "Native Vulkan rendering is unavailable. Install the Vulkan SDK and glslc, then reconfigure.\n";
        return 1;
    }
    return run_software_graphics_demo(use_window);
}


int run_switch2_cpu_demo() {
    using lucid::core::MemoryBus;
    using lucid::switch2::CpuCluster;
    constexpr std::uint64_t base = 0x1000U;
    constexpr std::size_t region_size = 0x1000U;
    constexpr std::uint64_t instruction_budget = CpuCluster::CoreCount * 2U;
    MemoryBus memory;
    std::string error;
    if (!memory.map_region(base, region_size, &error)) {
        std::cerr << "Switch 2 CPU demo memory setup failed: " << error << "\n";
        return 1;
    }

    CpuCluster cluster;
    for (std::size_t core = 0; core < CpuCluster::CoreCount; ++core) {
        const std::uint32_t immediate = 42U + static_cast<std::uint32_t>(core);
        const std::uint32_t movz_x0 = 0xD2800000U | (immediate << 5U);
        constexpr std::uint32_t branch_to_self = 0x14000000U;
        const std::uint64_t entry = base + static_cast<std::uint64_t>(core) * 8U;
        std::array<std::uint8_t, 8U> program{};
        const std::array<std::uint32_t, 2U> words{movz_x0, branch_to_self};
        for (std::size_t word = 0; word < words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                program[word * 4U + byte] = static_cast<std::uint8_t>(
                    (words[word] >> (byte * 8U)) & 0xFFU);
            }
        }
        if (!memory.write(entry, program, &error) ||
            !cluster.start_core(core, entry)) {
            std::cerr << "Switch 2 CPU core startup failed at core "
                      << core << ": " << error << "\n";
            return 1;
        }
    }

    const auto report = cluster.run_round_robin(memory, instruction_budget);
    if (report.instructions_executed != instruction_budget ||
        report.runnable_cores != CpuCluster::CoreCount ||
        report.faulted_cores != 0U) {
        std::cerr << "Switch 2 CPU cluster did not complete its bounded smoke test.\n";
        return 1;
    }

    std::cout << "Switch 2 eight-core CPU-context smoke test\n";
    for (std::size_t core = 0; core < CpuCluster::CoreCount; ++core) {
        const auto* context = cluster.context(core);
        if (context == nullptr || context->instructions_retired != 2U ||
            context->x[0] != 42ULL + static_cast<std::uint64_t>(core)) {
            std::cerr << "Unexpected architectural state on core " << core << ".\n";
            return 1;
        }
        std::cout << "  Core " << core << ": X0=" << context->x[0]
                  << ", retired=" << context->instructions_retired
                  << ", state=" << lucid::switch2::to_string(cluster.state(core))
                  << "\n";
    }
    std::cout << "[PASS] eight independent A64 contexts share one guest memory bus\n"
              << "[PASS] deterministic one-instruction round-robin execution\n"
              << "This is a CPU-cluster foundation only; no Horizon OS, Switch 2 GPU, "
                 "game loader, or commercial-game compatibility is implied.\n";
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

int inspect_switch_executable(const std::filesystem::path& path) {
    lucid::switchfmt::ExecutableInfo info{};
    std::string error;
    if (!lucid::switchfmt::inspect_executable_file(path, info, &error)) {
        std::cerr << "Switch executable inspection failed: " << error << "\n";
        return 1;
    }

    std::cout << "Executable format: " << lucid::switchfmt::to_string(info.format) << "\n"
              << "Version: " << info.version << "\n"
              << "Flags: 0x" << std::hex << info.flags << std::dec << "\n"
              << "Input bytes: " << info.input_size << "\n"
              << "BSS bytes: " << info.bss_size << "\n";
    if (info.format == lucid::switchfmt::ExecutableFormat::Nro0) {
        std::cout << "Declared image bytes: " << info.declared_image_size << "\n";
        std::cout << "Entry stub instruction: 0x" << std::hex
                  << info.entry_stub_instruction << std::dec << "\n"
                  << "MOD0 header offset: 0x" << std::hex
                  << info.module_header_location << std::dec << "\n"
                  << "Additional header offset: 0x" << std::hex
                  << info.additional_header_offset << std::dec << "\n"
                  << "ASET assets: " << (info.has_asset_section ? "present" : "not detected") << "\n";
    }
    if (info.uses_zstd_compression)
        std::cout << "Compression: Zstd-based NSO segments are declared\n";

    std::cout << "Build ID: ";
    for (const std::uint8_t byte : info.build_id) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(byte);
    }
    std::cout << std::dec << std::setfill(' ') << "\n";

    for (const auto& segment : info.segments) {
        std::cout << lucid::switchfmt::to_string(segment.kind)
                  << ": file=0x" << std::hex << segment.file_offset
                  << " stored=0x" << segment.stored_size
                  << " memory=0x" << segment.memory_offset
                  << " size=0x" << segment.memory_size << std::dec
                  << " compressed=" << (segment.compressed ? "yes" : "no")
                  << " hash-check=" << (segment.hash_required ? "yes" : "no")
                  << " execute-only=" << (segment.execute_only ? "yes" : "no")
                  << "\n";
    }
    std::cout << "Trailing bytes: " << info.trailing_bytes << "\n"
              << "Inspection only: no decryption, decompression, relocation, loading, or execution was performed.\n"
              << "NRO0/NSO0 metadata describes original Switch executable containers; it does not establish Switch 2 retail compatibility.\n";
    return 0;
}

int inspect_nsp_package(const std::filesystem::path& path) {
    lucid::switchfmt::NspPackageInfo info{};
    std::string error;
    if (!lucid::switchfmt::inspect_nsp_file(path, info, &error)) {
        std::cerr << "NSP import failed: " << error << "\n";
        return 1;
    }

    std::cout << "NSP package recognized (PFS0 directory valid)\n"
              << "Package bytes: " << info.package_size << "\n"
              << "Directory entries: " << info.file_count << "\n"
              << "NCA files: " << info.nca_count << "\n"
              << "CNMT-named NCA files: " << info.metadata_nca_count << "\n"
              << "Ticket files: " << info.ticket_count << "\n"
              << "Certificate files: " << info.certificate_count << "\n"
              << "Other files: " << info.other_file_count << "\n"
              << "PFS0 file-data offset: 0x" << std::hex
              << info.pfs0_data_offset << std::dec << "\n";

    std::cout << "Package contents:\n";
    for (const auto& file : info.files) {
        std::cout << "  " << file.name << " (" << file.size << " bytes)\n";
    }

    std::cout << "\nRecognized container only. NCA contents were not read or decrypted.\n"
              << "Not launchable: protected content handling, application loading, Horizon services, and guest GPU emulation are not implemented.\n"
              << "No ticket/title-key material was extracted or processed.\n";
    return 0;
}

int run_nro_homebrew(const std::filesystem::path& path,
                     const std::uint64_t instruction_budget) {
    lucid::switchfmt::NroHomebrewRunReport report{};
    std::string error;
    if (!lucid::switchfmt::run_restricted_nro_homebrew(
            path, instruction_budget, report, &error)) {
        std::cerr << "Restricted NRO loader rejected the file: " << error << "\n";
        return 1;
    }

    std::cout << "Restricted original-Switch NRO homebrew execution\n"
              << "Image base: 0x" << std::hex << report.image_base << "\n"
              << "Entry point: 0x" << report.entry_point << "\n"
              << "Final PC: 0x" << report.program_counter << "\n"
              << "Stack pointer: 0x" << report.stack_pointer << std::dec << "\n"
              << "Instruction budget: " << report.instruction_budget << "\n"
              << "Instructions retired: " << report.instructions_retired << "\n"
              << "X0: 0x" << std::hex << report.register_x0 << std::dec << "\n"
              << "Run result: " << lucid::core::to_string(report.run_result) << "\n"
              << "Last CPU step: " << lucid::core::to_string(report.last_step) << "\n"
              << "Runtime: bounded A64 interpreter; no Horizon OS or Switch GPU.\n"
              << "MOD0 relative relocations applied: " << report.relocations_applied << "\n"
              << "Unsupported: symbol-based relocations, dynamic imports, Horizon services, thread execution, audio, input, and display.\n";

    if (report.run_result != lucid::core::RunResult::InstructionLimitReached) {
        std::cerr << "The program reached an unsupported instruction or memory path; it did not complete a game session.\n";
        return 1;
    }
    std::cout << "[PASS] restricted NRO image loaded and ran to its instruction budget.\n";
    return 0;
}

int run_compatibility_report(const std::string_view target) {
    using lucid::switchfmt::HardwareTarget;
    if (target == "switch" || target == "all") {
        lucid::switchfmt::write_compatibility_report(
            HardwareTarget::OriginalSwitch, std::cout);
        if (target == "all") std::cout << "\n";
    }
    if (target == "switch2" || target == "all") {
        lucid::switchfmt::write_compatibility_report(
            HardwareTarget::Switch2, std::cout);
    }
    if (target != "switch" && target != "switch2" && target != "all") {
        std::cerr << "Target must be switch, switch2, or all.\n";
        return 2;
    }
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
    if (command == "--switch2-cpu-demo") return run_switch2_cpu_demo();
    if (command == "--graphics-demo") {
        bool use_window = false;
        bool force_vulkan = false;
        for (int index = 2; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--window") use_window = true;
            else if (option == "--vulkan") force_vulkan = true;
            else {
                std::cerr << "Unknown graphics-demo option: " << option << "\n";
                return 2;
            }
        }
        return run_graphics_demo(use_window, force_vulkan);
    }
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
    if (command == "--inspect-nsp") {
        if (argc != 3) {
            std::cerr << "Usage: --inspect-nsp <game.nsp>\n";
            return 2;
        }
        return inspect_nsp_package(argv[2]);
    }
    if (command == "--inspect-switch-executable") {
        if (argc != 3) {
            std::cerr << "Usage: --inspect-switch-executable <program.nro|program.nso>\n";
            return 2;
        }
        return inspect_switch_executable(argv[2]);
    }
    if (command == "--compat-report") {
        if (argc != 3) {
            std::cerr << "Usage: --compat-report <switch|switch2|all>\n";
            return 2;
        }
        return run_compatibility_report(argv[2]);
    }
    if (command == "--run-nro") {
        if (argc < 3 || argc > 4) {
            std::cerr << "Usage: --run-nro <homebrew.nro> [instruction-budget]\n";
            return 2;
        }
        std::uint64_t budget = lucid::switchfmt::DefaultNroInstructionBudget;
        if (argc == 4 && !parse_instruction_budget(argv[3], budget)) {
            std::cerr << "Instruction budget must be between 1 and "
                      << lucid::switchfmt::MaximumNroInstructionBudget << ".\n";
            return 2;
        }
        return run_nro_homebrew(argv[2], budget);
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
