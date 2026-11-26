#include "lucid/core/emulator.hpp"
#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
namespace {
int failures = 0;
void expect(bool condition, const char* message) {
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
    expect(!bus.map_region(0x3000U, 8U, &error,
        lucid::core::MemoryPermission::None), "reject permissionless region");
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
    lucid::core::MemoryBus protected_bus;
    expect(protected_bus.map_region(0x4000U, 16U, &error,
        lucid::core::MemoryPermission::Read), "map read-only memory");
    expect(!protected_bus.write8(0x4000U, 0xAAU, &error),
           "reject writes to read-only memory");
    expect(protected_bus.read8(0x4000U, value, &error),
           "allow reads from read-only memory");
    lucid::core::MemoryBus code_bus;
    expect(code_bus.map_region(0x5000U, 16U, &error,
        lucid::core::MemoryPermission::ReadWrite), "map non-executable memory");
    const std::array<std::uint8_t, 4U> nop{0x1FU, 0x20U, 0x03U, 0xD5U};
    expect(code_bus.write(0x5000U, nop, &error), "write instruction bytes");
    std::array<std::uint8_t, 4U> instruction{};
    expect(!code_bus.read_instruction(0x5000U, instruction, &error),
           "reject instruction fetch without execute permission");
    lucid::cpu::A64Interpreter cpu;
    lucid::cpu::CpuContext context{};
    context.program_counter = 0x5000U;
    expect(cpu.step(context, code_bus) == lucid::cpu::StepResult::MemoryFault,
           "CPU reports execute-permission fault");
}
void test_emulator_execution() {
    using namespace lucid::core;
    constexpr std::uint64_t base = 0x8000U;
    const EmulatorConfig config{base, 0x1000U, base};
    Emulator emulator;
    std::string error;
    expect(emulator.run() == RunResult::NotInitialized,
           "refuse execution before initialization");
    expect(emulator.initialize(config, &error), "initialize CPU-backed session");
    expect(emulator.state() == SessionState::Ready, "session becomes ready");
    expect(emulator.stage1_mmu().configured(), "emulator configures ARM stage-1 translation");
    expect(emulator.cpu_context().system_registers.read(
        lucid::arch::SystemRegister::TTBR0_EL1) == emulator.stage1_mmu().ttbr0_el1(),
        "emulator exposes its TTBR0 bootstrap value");
    expect(!emulator.initialize(config, &error), "reject duplicate initialization");
    const std::array<std::uint8_t, 4U> loop{0x00U, 0x00U, 0x00U, 0x14U};
    expect(emulator.load_program(base, loop, &error), "load synthetic A64 program");
    expect(!emulator.load_program(base + 0x2000U, loop, &error),
           "reject program writes outside guest memory");
    expect(!emulator.load_program(emulator.stage1_mmu().ttbr0_el1(), loop, &error),
           "reject writes to reserved stage-1 page tables");
    expect(emulator.run(7U) == RunResult::InstructionLimitReached,
           "execute until instruction budget is reached");
    expect(emulator.state() == SessionState::Paused, "budget exhaustion pauses");
    expect(emulator.cpu_context().instructions_retired == 7U &&
           emulator.cpu_context().program_counter == base,
           "branch loop runs and preserves expected PC");
    expect(emulator.stage1_mmu().tlb_entries() > 0U &&
           emulator.stage1_mmu().tlb_hits() > 0U,
           "emulator instruction fetches fill and hit the TLB");
    expect(emulator.run(3U) == RunResult::InstructionLimitReached,
           "resume paused session");
    expect(emulator.cpu_context().instructions_retired == 10U,
           "retired counter continues after resume");
    expect(emulator.stop(), "stop paused session");
    expect(emulator.state() == SessionState::Stopped, "session enters stopped state");
    expect(emulator.run(1U) == RunResult::InvalidState,
           "refuse execution after stop");
    Emulator faulting;
    expect(faulting.initialize(config, &error), "initialize fault-test session");
    const std::array<std::uint8_t, 4U> invalid{0xFFU, 0xFFU, 0xFFU, 0xFFU};
    expect(faulting.load_program(base, invalid, &error), "load unsupported opcode");
    expect(faulting.run(1U) == RunResult::CpuFault,
           "surface unsupported opcode as CPU fault");
    expect(faulting.state() == SessionState::Faulted &&
           faulting.last_step_result() == lucid::cpu::StepResult::UnsupportedInstruction,
           "retain reason for CPU fault");
    Emulator invalid_config;
    const EmulatorConfig misaligned{base, 0x1000U, base + 2U};
    expect(!invalid_config.initialize(misaligned, &error),
           "reject misaligned entry point");
    expect(invalid_config.state() == SessionState::Cold,
           "failed initialization leaves session cold");
}
}
int main() {
    test_memory_bus();
    test_emulator_execution();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All foundation tests passed.\n";
    return 0;
}
