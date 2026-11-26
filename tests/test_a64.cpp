#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"
#include "lucid/cpu/cpu_memory.hpp"
#include "lucid/mmu/address_space.hpp"

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

    // ADDS XZR,X1,#0 sets flags from 50; SUBS XZR,X1,#50 sets Z and C.
    write32(memory, base + 0x18U, 0xF100003FU);
    write32(memory, base + 0x1CU, 0xF100C83FU);
    write32(memory, base + 0x20U, 0x54000040U); // B.EQ +8
    state.x[1] = 50U;
    state.program_counter = base + 0x18U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute SUBS immediate and set NZCV");
    expect(!state.flags.negative && !state.flags.zero && state.flags.carry &&
           !state.flags.overflow, "SUBS updates N, Z, C and V for positive result");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute SUBS to zero");
    expect(state.flags.zero && state.flags.carry && !state.flags.negative &&
           !state.flags.overflow, "SUBS sets zero and no-borrow flags");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute conditional B.EQ");
    expect(state.program_counter == base + 0x28U,
           "B.EQ branches when Z flag is set");

    // A non-taken conditional branch advances normally.
    write32(memory, base + 0x2CU, 0x54000041U); // B.NE +8
    state.program_counter = base + 0x2CU;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "non-taken B.NE does not branch");
    expect(state.program_counter == base + 0x30U,
           "non-taken conditional branch advances PC");

    // B +8 skips the next word; the branch target is PC+8.
    write32(memory, base + 0x40U, 0x14000002U);
    state.program_counter = base + 0x40U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute unconditional branch");
    expect(state.program_counter == base + 0x48U, "branch target is PC-relative");

    // CBZ X3,+8, with X3 initially zero.
    write32(memory, base + 0x50U, 0xB4000043U);
    state.x[3] = 0U;
    state.program_counter = base + 0x50U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute CBZ");
    expect(state.program_counter == base + 0x58U, "CBZ takes branch when zero");

    // RET X30.
    write32(memory, base + 0x60U, 0xD65F03C0U);
    state.x[30] = base + 0x70U;
    state.program_counter = base + 0x60U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute RET");
    expect(state.program_counter == base + 0x70U, "RET loads PC from X30");

    // Register arithmetic/logical operations.
    write32(memory, base + 0x70U, 0x8B020020U); // ADD X0,X1,X2
    write32(memory, base + 0x74U, 0xCB020023U); // SUB X3,X1,X2
    write32(memory, base + 0x78U, 0x8A020024U); // AND X4,X1,X2
    write32(memory, base + 0x7CU, 0xAA020025U); // ORR X5,X1,X2
    write32(memory, base + 0x80U, 0xCA020026U); // EOR X6,X1,X2
    write32(memory, base + 0x84U, 0xEA02003FU); // ANDS XZR,X1,X2
    state.x[1] = 0xF0F0U;
    state.x[2] = 0x0FF0U;
    state.program_counter = base + 0x70U;
    for (unsigned index = 0; index < 6U; ++index) {
        expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
               "execute register arithmetic/logical instruction");
    }
    expect(state.x[0] == 0x100E0U, "ADD shifted-register computes sum");
    expect(state.x[3] == 0xE100U, "SUB shifted-register computes difference");
    expect(state.x[4] == 0x00F0U, "AND computes bitwise intersection");
    expect(state.x[5] == 0xFFF0U, "ORR computes bitwise union");
    expect(state.x[6] == 0xFF00U, "EOR computes bitwise exclusive-or");
    expect(!state.flags.negative && !state.flags.zero &&
           !state.flags.carry && !state.flags.overflow,
           "ANDS updates logical NZCV flags");

    // ADD X7,SP,#0 exercises ADD immediate SP source semantics.
    write32(memory, base + 0x88U, 0x910003E7U);
    state.sp = 0x123456789ABCDEF0ULL;
    state.program_counter = base + 0x88U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute ADD immediate with SP source");
    expect(state.x[7] == state.sp, "ADD immediate reads SP when Rn is 31");

    // Scalar unsigned-offset stores and loads: STR/LDR B, H, W and X.
    write32(memory, base + 0x90U, 0x39000480U); // STRB W0,[X4,#1]
    write32(memory, base + 0x94U, 0x39400481U); // LDRB W1,[X4,#1]
    write32(memory, base + 0x98U, 0x79000482U); // STRH W2,[X4,#2]
    write32(memory, base + 0x9CU, 0x79400483U); // LDRH W3,[X4,#2]
    write32(memory, base + 0xA0U, 0xB9000485U); // STR W5,[X4,#4]
    write32(memory, base + 0xA4U, 0xB9400486U); // LDR W6,[X4,#4]
    write32(memory, base + 0xA8U, 0xF9000487U); // STR X7,[X4,#8]
    write32(memory, base + 0xACU, 0xF9400488U); // LDR X8,[X4,#8]
    state.x[0] = 0xA5U;
    state.x[2] = 0xBEEFU;
    state.x[4] = base + 0xC0U;
    state.x[5] = 0x12345678U;
    state.x[7] = 0x1122334455667788ULL;
    state.program_counter = base + 0x90U;
    for (unsigned index = 0; index < 8U; ++index) {
        expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
               "execute scalar unsigned-offset LDR/STR");
    }
    expect(state.x[1] == 0xA5U, "LDRB zero-extends an 8-bit value");
    expect(state.x[3] == 0xBEEFU, "LDRH zero-extends a 16-bit value");
    expect(state.x[6] == 0x12345678U, "LDR W loads a 32-bit value");
    expect(state.x[8] == 0x1122334455667788ULL, "LDR X loads a 64-bit value");

    // Base register 31 denotes SP in the memory addressing mode.
    write32(memory, base + 0xB0U, 0xF9000BE9U); // STR X9,[SP,#16]
    write32(memory, base + 0xB4U, 0xF9400BEAU); // LDR X10,[SP,#16]
    state.sp = base + 0xC0U;
    state.x[9] = 0x8877665544332211ULL;
    state.program_counter = base + 0xB0U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "STR uses SP when Rn is 31");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "LDR uses SP when Rn is 31");
    expect(state.x[10] == 0x8877665544332211ULL,
           "SP-relative load returns the stored 64-bit value");

    // A faulting 64-bit access must not partially update CPU state.
    write32(memory, base + 0xB8U, 0xF940000BU); // LDR X11,[X4]
    write32(memory, base + 0xBCU, 0xF900000CU); // STR X12,[X4]
    state.x[4] = base + 0xFCU; // only four bytes remain in this mapping
    state.x[11] = 0xCAFEBABEULL;
    state.x[12] = 0xDEADBEEFULL;
    state.program_counter = base + 0xB8U;
    const auto before_load_fault = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MemoryFault,
           "LDR reports a cross-boundary memory fault");
    expect(state.x[11] == 0xCAFEBABEULL &&
           state.program_counter == base + 0xB8U &&
           state.instructions_retired == before_load_fault,
           "faulting LDR leaves CPU state unchanged");
    state.program_counter = base + 0xBCU;
    const auto before_store_fault = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MemoryFault,
           "STR reports a cross-boundary memory fault");
    expect(state.program_counter == base + 0xBCU &&
           state.instructions_retired == before_store_fault,
           "faulting STR leaves PC and retired count unchanged");

    // Unknown instruction must be rejected without mutating CPU state.
    write32(memory, base + 0x80U, 0xFFFFFFFFU);
    state.program_counter = base + 0x80U;
    const auto retired_before = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::UnsupportedInstruction,
           "reject unsupported instruction");
    expect(state.program_counter == base + 0x80U &&
           state.instructions_retired == retired_before,
           "unsupported instruction leaves state unchanged");

    state.program_counter = base + 0x81U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MisalignedProgramCounter,
           "reject unaligned program counter");

    state.program_counter = 0x9000U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MemoryFault,
           "report instruction fetch outside mapped memory");

    // MRS/MSR access only the explicitly modelled system-register bank.
    const std::uint32_t mrs_current_el =
        0xD5200000U | (0xC212U << 5U) | 9U;
    const std::uint32_t msr_vbar =
        0xD5000000U | (0xC600U << 5U) | 3U;
    const std::uint32_t mrs_vbar =
        0xD5200000U | (0xC600U << 5U) | 4U;
    const std::uint32_t msr_current_el =
        0xD5000000U | (0xC212U << 5U) | 3U;
    write32(memory, base + 0xD0U, mrs_current_el);
    write32(memory, base + 0xD4U, msr_vbar);
    write32(memory, base + 0xD8U, mrs_vbar);
    write32(memory, base + 0xDCU, msr_current_el);
    state.program_counter = base + 0xD0U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute MRS CurrentEL");
    expect(state.x[9] == 0U, "MRS CurrentEL returns the reset EL0 value");
    state.system_registers.set_exception_level(1U);
    state.x[3] = 0x8000U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute MSR VBAR_EL1");
    expect(state.system_registers.read(lucid::arch::SystemRegister::VBAR_EL1) == 0x8000U,
           "MSR updates the selected VBAR register");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute MRS VBAR_EL1");
    expect(state.x[4] == 0x8000U, "MRS reads back VBAR_EL1");
    state.program_counter = base + 0xDCU;
    const auto system_fault_pc = state.program_counter;
    const auto system_fault_retired = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::SystemRegisterFault,
           "reject writes to read-only CurrentEL");
    expect(state.program_counter == system_fault_pc &&
           state.instructions_retired == system_fault_retired,
           "faulting MSR leaves PC and retired count unchanged");

    // The CPU memory facade now translates instruction fetches and data accesses.
    lucid::core::MemoryBus physical_memory;
    expect(physical_memory.map_region(0x3000U, 0x1000U),
           "map physical code page");
    expect(physical_memory.map_region(0x9000U, 0x1000U),
           "map physical data page");
    write32(physical_memory, 0x3000U, 0xF9400020U); // LDR X0,[X1]
    write32(physical_memory, 0x3004U, 0xF9000422U); // STR X2,[X1,#8]
    const std::array<std::uint8_t, 8> initial_data{
        0x88U, 0x77U, 0x66U, 0x55U, 0x44U, 0x33U, 0x22U, 0x11U
    };
    expect(physical_memory.write(0x9000U, initial_data),
           "seed translated physical data");
    lucid::mmu::AddressSpace translated_space;
    expect(translated_space.map_page(0x10000U, 0x3000U, true, false, true),
           "map virtual executable page");
    expect(translated_space.map_page(0x20000U, 0x9000U, true, true, false),
           "map virtual read/write data page");
    lucid::cpu::CpuMemory translated_memory(physical_memory, &translated_space);
    lucid::cpu::CpuContext translated_state{};
    translated_state.program_counter = 0x10000U;
    translated_state.x[1] = 0x20000U;
    translated_state.x[2] = 0xAABBCCDDEEFF0011ULL;
    expect(cpu.step(translated_state, translated_memory) ==
               lucid::cpu::StepResult::Executed,
           "fetch and execute through virtual-to-physical translation");
    expect(translated_state.x[0] == 0x1122334455667788ULL,
           "translated LDR reads from the physical page");
    expect(cpu.step(translated_state, translated_memory) ==
               lucid::cpu::StepResult::Executed,
           "translated STR writes to the physical page");
    std::array<std::uint8_t, 8> stored_data{};
    expect(physical_memory.read(0x9008U, stored_data) &&
           stored_data[0] == 0x11U && stored_data[7] == 0xAAU,
           "translated STR updates physical backing memory");

    expect(translated_space.unmap_page(0x20000U) &&
           translated_space.map_page(0x20000U, 0x9000U, true, false, false),
           "remap data page as read-only");
    translated_state.program_counter = 0x10004U;
    const auto translation_fault_pc = translated_state.program_counter;
    const auto translation_fault_retired = translated_state.instructions_retired;
    expect(cpu.step(translated_state, translated_memory) ==
               lucid::cpu::StepResult::MemoryFault,
           "translated store obeys page write permissions");
    expect(translated_state.program_counter == translation_fault_pc &&
           translated_state.instructions_retired == translation_fault_retired,
           "faulting translated store leaves CPU state unchanged");
    translated_state.program_counter = 0x20000U;
    expect(cpu.step(translated_state, translated_memory) ==
               lucid::cpu::StepResult::MemoryFault,
           "instruction fetch obeys page execute permissions");

    if (failures != 0) {
        std::cerr << failures << " A64 test(s) failed.\n";
        return 1;
    }
    std::cout << "All A64 subset tests passed.\n";
    return 0;
}
