#include "lucid/core/emulator.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
namespace {
constexpr const char* kVersion = "0.3.0";
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
        << "  lucidswitch2 --run        Execute a bounded synthetic A64 demo\n\n"
        << "Status: no console hardware emulation or commercial game loading.\n";
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
    std::cerr << "Unknown option: " << command << "\n\n";
    print_help();
    return 1;
}
