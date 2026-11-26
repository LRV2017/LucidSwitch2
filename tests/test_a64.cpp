#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"

#include <array>
#include <cstdint>
#include <iostream>

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

void write32(lucid::core::MemoryBus& memory, std::uint64_t address,
             std::uint32_t word) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(word & 0xFFU),
        static_cast<std::uint8_t>((word >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((word >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((word >> 24U) & 0xFFU)
    };
    expect(memory.write(address, bytes), "write synthetic instruction");
}
} // namespace

int main() {
    constexpr std::uint64_t base = 0x1000U;
    lucid::core::MemoryBus memory;
    expect(memory.map_region(base, 0x100U), "map synthetic instruction memory");

    // MOVZ X0,#42 ; ADD X1,X0,#8 ; SUB X2,X1,#10 ; NOP
    write32(memory, base + 0U, 0xD2800540U);
    write32(memory, base + 4U, 0x91002001U);
    write32(memory, base + 8U, 0xD1002822U);
    write32(memory, base + 12U, 0xD503201FU);

    lucid::cpu::A64Interpreter cpu;
    lucid::cpu::CpuContext state{};
    state.program_counter = base;

    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute MOVZ");
    expect(state.x[0] == 42U && state.program_counter == base + 4U,
           "MOVZ writes immediate and advances PC");

    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute ADD immediate");
    expect(state.x[1] == 50U, "ADD computes 42 + 8");

    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute SUB immediate");
    expect(state.x[2] == 40U, "SUB computes 50 - 10");

    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute NOP");
    expect(state.instructions_retired == 4U, "count retired instructions");

    // B +8 skips the next word; the branch target is PC+8.
    write32(memory, base + 0x20U, 0x14000002U);
    state.program_counter = base + 0x20U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute unconditional branch");
    expect(state.program_counter == base + 0x28U, "branch target is PC-relative");

    // CBZ X3,+8, with X3 initially zero.
    write32(memory, base + 0x30U, 0xB4000043U);
    state.x[3] = 0U;
    state.program_counter = base + 0x30U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute CBZ");
    expect(state.program_counter == base + 0x38U, "CBZ takes branch when zero");

    // RET X30.
    write32(memory, base + 0x40U, 0xD65F03C0U);
    state.x[30] = base + 0x50U;
    state.program_counter = base + 0x40U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute RET");
    expect(state.program_counter == base + 0x50U, "RET loads PC from X30");

    // Unknown instruction must be rejected without mutating CPU state.
    write32(memory, base + 0x60U, 0xFFFFFFFFU);
    state.program_counter = base + 0x60U;
    const auto retired_before = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::UnsupportedInstruction,
           "reject unsupported instruction");
    expect(state.program_counter == base + 0x60U &&
           state.instructions_retired == retired_before,
           "unsupported instruction leaves state unchanged");

    state.program_counter = base + 0x61U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MisalignedProgramCounter,
           "reject unaligned program counter");

    state.program_counter = 0x9000U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MemoryFault,
           "report instruction fetch outside mapped memory");

    if (failures != 0) {
        std::cerr << failures << " A64 test(s) failed.\n";
        return 1;
    }
    std::cout << "All A64 subset tests passed.\n";
    return 0;
}
