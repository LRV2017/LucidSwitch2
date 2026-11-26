#include "lucid/core/emulator.hpp"
#include "lucid/core/memory_bus.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {
int failures = 0;

void expect(const bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "[FAIL] " << message << "\n";
    } else {
        std::cout << "[PASS] " << message << "\n";
    }
}

void test_memory_bus() {
    lucid::core::MemoryBus bus;
    std::string error;

    expect(bus.map_region(0x1000U, 16U, &error), "map a memory region");
    expect(bus.region_count() == 1U, "count mapped regions");
    expect(!bus.map_region(0x1008U, 8U, &error), "reject overlapping regions");
    expect(!bus.map_region(0x2000U, 0U, &error), "reject empty regions");
    expect(bus.write8(0x1004U, 0x7BU, &error), "write one byte");

    std::uint8_t value = 0U;
    expect(bus.read8(0x1004U, value, &error) && value == 0x7BU,
           "read back the written byte");

    std::array<std::uint8_t, 2U> crossing{};
    expect(!bus.read(0x100FU, crossing, &error),
           "reject reads crossing a region boundary");
    expect(!bus.write8(0x3000U, 0x11U, &error),
           "reject writes to unmapped memory");

    lucid::core::MemoryBus overflow_bus;
    const auto max = std::numeric_limits<std::uint64_t>::max();
    expect(!overflow_bus.map_region(max - 3U, 8U, &error),
           "reject address-range overflow");
}

void test_emulator_lifecycle() {
    lucid::core::Emulator emulator;
    std::string error;

    expect(emulator.state() == lucid::core::SessionState::Cold,
           "new session begins cold");
    expect(emulator.run() == lucid::core::RunResult::NotInitialized,
           "refuse execution before initialization");
    expect(emulator.initialize(&error), "initialize session");
    expect(emulator.state() == lucid::core::SessionState::Ready,
           "initialized session becomes ready");
    expect(!emulator.initialize(&error), "reject duplicate initialization");
    expect(emulator.run() == lucid::core::RunResult::BackendUnavailable,
           "refuse execution without a CPU backend");
    expect(emulator.state() == lucid::core::SessionState::Ready,
           "failed backend check leaves session ready");
}
} // namespace

int main() {
    test_memory_bus();
    test_emulator_lifecycle();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All foundation tests passed.\n";
    return 0;
}
