#include "lucid/core/emulator.hpp"
#include "lucid/core/memory_bus.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {
constexpr const char* kVersion = "0.1.0";

void print_help() {
    std::cout
        << "LucidSwitch2 " << kVersion << "\n"
        << "Clean-room console emulation research scaffold\n\n"
        << "Usage:\n"
        << "  lucidswitch2 --help       Show this help\n"
        << "  lucidswitch2 --version    Show version\n"
        << "  lucidswitch2 --self-test  Run local foundation checks\n"
        << "  lucidswitch2 --run        Check execution backend availability\n\n"
        << "Status: no console CPU/GPU execution or commercial game loading is implemented.\n";
}

int run_self_test() {
    lucid::core::MemoryBus bus;
    std::string error;

    if (!bus.map_region(0x1000U, 0x1000U, &error)) {
        std::cerr << "Self-test failed while mapping memory: " << error << "\n";
        return 1;
    }
    if (!bus.write8(0x1000U, 0xA5U, &error)) {
        std::cerr << "Self-test failed while writing memory: " << error << "\n";
        return 1;
    }

    std::uint8_t value = 0U;
    if (!bus.read8(0x1000U, value, &error) || value != 0xA5U) {
        std::cerr << "Self-test failed while reading memory: " << error << "\n";
        return 1;
    }

    lucid::core::Emulator emulator;
    if (!emulator.initialize(&error) ||
        emulator.state() != lucid::core::SessionState::Ready) {
        std::cerr << "Self-test failed while initializing session: " << error << "\n";
        return 1;
    }

    std::cout << "[PASS] memory mapping and read/write\n"
              << "[PASS] emulator session initialization\n"
              << "Foundation self-test passed. This does not test console compatibility.\n";
    return 0;
}

int run_backend_check() {
    lucid::core::Emulator emulator;
    std::string error;
    if (!emulator.initialize(&error)) {
        std::cerr << "Initialization failed: " << error << "\n";
        return 1;
    }

    const auto result = emulator.run();
    std::cout << "Session state: " << lucid::core::to_string(emulator.state()) << "\n"
              << "Run result: " << lucid::core::to_string(result) << "\n"
              << "A console CPU backend must be implemented before software can execute.\n";
    return result == lucid::core::RunResult::Started ? 0 : 2;
}
} // namespace

int main(const int argc, char* argv[]) {
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
    if (command == "--self-test") {
        return run_self_test();
    }
    if (command == "--run") {
        return run_backend_check();
    }

    std::cerr << "Unknown option: " << command << "\n\n";
    print_help();
    return 1;
}
