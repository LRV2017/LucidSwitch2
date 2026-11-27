#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"
#include "lucid/cpu/cpu_memory.hpp"
#include "lucid/mmu/address_space.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <atomic>
#include <thread>

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


    // Scalar floating-point register transfers and arithmetic (S/D lanes).
    write32(memory, base + 0x00U, 0x1E270020U); // FMOV S0,W1
    write32(memory, base + 0x04U, 0x1E260002U); // FMOV W2,S0
    write32(memory, base + 0x08U, 0x1E252883U); // FADD S3,S4,S5
    write32(memory, base + 0x0CU, 0x1E6838E6U); // FSUB D6,D7,D8
    write32(memory, base + 0x10U, 0x1E6B0949U); // FMUL D9,D10,D11
    write32(memory, base + 0x14U, 0x1E2E19ACU); // FDIV S12,S13,S14
    write32(memory, base + 0x18U, 0x9E67020FU); // FMOV D15,X16
    write32(memory, base + 0x1CU, 0x9E6601F1U); // FMOV X17,D15
    write32(memory, base + 0x20U, 0x1E204072U); // FMOV S18,S3
    write32(memory, base + 0x24U, 0x1E6040D3U); // FMOV D19,D6

    state = lucid::cpu::CpuContext{};
    state.program_counter = base;
    state.x[1] = 0x40400000U; // 3.0f
    state.v[4].low = std::bit_cast<std::uint32_t>(1.5F);
    state.v[5].low = std::bit_cast<std::uint32_t>(2.25F);
    state.v[7].low = std::bit_cast<std::uint64_t>(5.5);
    state.v[8].low = std::bit_cast<std::uint64_t>(2.0);
    state.v[10].low = std::bit_cast<std::uint64_t>(1.5);
    state.v[11].low = std::bit_cast<std::uint64_t>(4.0);
    state.v[13].low = std::bit_cast<std::uint32_t>(9.0F);
    state.v[14].low = std::bit_cast<std::uint32_t>(3.0F);
    state.x[16] = std::bit_cast<std::uint64_t>(6.25);
    state.flags = {true, false, true, true};
    for (unsigned index = 0U; index < 10U; ++index) {
        expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
               "execute scalar FP move or arithmetic instruction");
    }
    expect(state.x[2] == 0x40400000U,
           "FMOV Wd,Sn transfers the scalar S lane bit-for-bit");
    expect(state.v[3].low == std::bit_cast<std::uint32_t>(3.75F),
           "FADD S computes single-precision arithmetic");
    expect(state.v[6].low == std::bit_cast<std::uint64_t>(3.5),
           "FSUB D computes double-precision arithmetic");
    expect(state.v[9].low == std::bit_cast<std::uint64_t>(6.0),
           "FMUL D computes double-precision arithmetic");
    expect(state.v[12].low == std::bit_cast<std::uint32_t>(3.0F),
           "FDIV S computes single-precision arithmetic");
    expect(state.x[17] == std::bit_cast<std::uint64_t>(6.25),
           "FMOV Xd,Dn transfers the scalar D lane bit-for-bit");
    expect(state.v[0].low == 0x40400000U &&
           state.v[0].high == 0U && state.v[18].low == state.v[3].low &&
           state.v[18].high == 0U &&
           state.v[19].low == state.v[6].low &&
           state.v[19].high == 0U,
           "scalar FMOV writes the selected lane and clears modeled upper lanes");
    expect(state.flags.negative && !state.flags.zero &&
           state.flags.carry && state.flags.overflow,
           "scalar FP instructions leave integer NZCV flags unchanged");

    // An unsupported FP type/encoding must fail without mutating CPU state.
    write32(memory, base + 0x28U, 0x1EA20803U);
    state.program_counter = base + 0x28U;
    const auto fp_unsupported_retired = state.instructions_retired;
    const auto fp_unsupported_x3 = state.x[3];
    expect(cpu.step(state, memory) ==
               lucid::cpu::StepResult::UnsupportedInstruction,
           "reject unsupported scalar FP type encoding");
    expect(state.program_counter == base + 0x28U &&
           state.instructions_retired == fp_unsupported_retired &&
           state.x[3] == fp_unsupported_x3,
           "unsupported scalar FP encoding leaves CPU state unchanged");


    // Common scalar integer instructions needed by compiled workloads.
    write32(memory, base + 0x00U, 0x92824680U); // MOVN X0,#0x1234
    write32(memory, base + 0x04U, 0x10000041U); // ADR X1,+8
    write32(memory, base + 0x08U, 0x9AC40862U); // UDIV X2,X3,X4
    write32(memory, base + 0x0CU, 0x9AC70CC5U); // SDIV X5,X6,X7
    state = lucid::cpu::CpuContext{};
    state.program_counter = base;
    state.x[3] = 100U;
    state.x[4] = 9U;
    state.x[6] = static_cast<std::uint64_t>(-81LL);
    state.x[7] = 9U;
    for (unsigned index = 0U; index < 4U; ++index) {
        expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
               "execute MOVN, ADR and signed/unsigned divide instructions");
    }
    expect(state.x[0] == 0xFFFFFFFFFFFFEDCBULL,
           "MOVN inverts the selected 16-bit immediate across X register");
    expect(state.x[1] == base + 12U,
           "ADR creates a signed PC-relative address");
    expect(state.x[2] == 11U,
           "UDIV computes unsigned integer quotient");
    expect(state.x[5] == static_cast<std::uint64_t>(-9LL),
           "SDIV computes signed integer quotient");

    // Logical-immediate bitmask and variable shifts.
    write32(memory, base + 0x10U, 0x92401C30U); // AND X16,X1,#0xff
    write32(memory, base + 0x14U, 0x9AC22024U); // LSLV X4,X1,X2
    write32(memory, base + 0x18U, 0x9AC22425U); // LSRV X5,X1,X2
    write32(memory, base + 0x1CU, 0x9AC22826U); // ASRV X6,X1,X2
    write32(memory, base + 0x20U, 0x9AC22C27U); // RORV X7,X1,X2
    state.x[1] = 0xF123456789ABCDEFULL;
    state.x[2] = 4U;
    state.program_counter = base + 0x10U;
    for (unsigned index = 0U; index < 5U; ++index) {
        expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
               "execute logical immediate or variable shift instruction");
    }
    expect(state.x[16] == 0xEFU,
           "logical immediate expands a replicated bitmask correctly");
    expect(state.x[4] == (state.x[1] << 4U),
           "LSLV masks the shift count and shifts left");
    expect(state.x[5] == (state.x[1] >> 4U),
           "LSRV shifts right logically");
    expect(state.x[6] == 0xFF123456789ABCDEULL,
           "ASRV sign-extends a negative 64-bit operand");
    expect(state.x[7] == ((state.x[1] >> 4U) | (state.x[1] << 60U)),
           "RORV rotates within the 64-bit operand width");

    // MADD/MSUB and conditional-select instructions.
    write32(memory, base + 0x24U, 0x9B020C28U); // MADD X8,X1,X2,X3
    write32(memory, base + 0x28U, 0x9B028C29U); // MSUB X9,X1,X2,X3
    write32(memory, base + 0x2CU, 0x9A82002CU); // CSEL X12,X1,X2,EQ
    write32(memory, base + 0x30U, 0x9A82042DU); // CSINC X13,X1,X2,EQ
    write32(memory, base + 0x34U, 0xDA82002EU); // CSINV X14,X1,X2,EQ
    write32(memory, base + 0x38U, 0xDA82042FU); // CSNEG X15,X1,X2,EQ
    state.x[1] = 7U;
    state.x[2] = 3U;
    state.x[3] = 5U;
    state.flags.zero = true;
    state.program_counter = base + 0x24U;
    for (unsigned index = 0U; index < 6U; ++index) {
        expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
               "execute multiply-accumulate or conditional-select instruction");
    }
    expect(state.x[8] == 26U, "MADD computes product plus accumulator");
    expect(state.x[9] == 0xFFFFFFFFFFFFFFF0ULL,
           "MSUB computes accumulator minus product");
    expect(state.x[12] == 7U && state.x[13] == 7U &&
           state.x[14] == 7U && state.x[15] == 7U,
           "conditional-select family uses the first operand when condition is true");

    // BL and BLR must preserve the return address in X30.
    write32(memory, base + 0x3CU, 0x94000002U); // BL +8
    state.program_counter = base + 0x3CU;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute BL immediate");
    expect(state.x[30] == base + 0x40U && state.program_counter == base + 0x44U,
           "BL stores the following instruction address in X30");
    write32(memory, base + 0x44U, 0xD63F00A0U); // BLR X5
    state.x[5] = base + 0x60U;
    state.program_counter = base + 0x44U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Branch,
           "execute BLR register");
    expect(state.x[30] == base + 0x48U && state.program_counter == base + 0x60U,
           "BLR stores the following instruction address in X30");


    // Integer paired loads/stores: offset, pre-index, and post-index modes.
    // STP X3,X4,[X2,#16]; LDP X0,X1,[X2,#16]
    write32(memory, base + 0xD0U, 0xA9011043U);
    write32(memory, base + 0xD4U, 0xA9410440U);
    // STP X5,X6,[X2,#-16]!; LDP X7,X8,[X2],#16
    write32(memory, base + 0xD8U, 0xA9BF1845U);
    write32(memory, base + 0xDCU, 0xA8C12047U);
    state = lucid::cpu::CpuContext{};
    state.program_counter = base + 0xD0U;
    state.x[2] = base + 0xB0U;
    state.x[3] = 0x1122334455667788ULL;
    state.x[4] = 0x8877665544332211ULL;
    state.x[5] = 0xCAFEBABE12345678ULL;
    state.x[6] = 0x0F1E2D3C4B5A6978ULL;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute STP with signed-offset addressing");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute LDP with signed-offset addressing");
    expect(state.x[0] == state.x[3] && state.x[1] == state.x[4],
           "LDP restores both 64-bit registers stored by STP");
    expect(state.x[2] == base + 0xB0U,
           "offset pair addressing leaves its base register unchanged");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute STP with pre-index writeback");
    expect(state.x[2] == base + 0xA0U,
           "pre-index STP updates the base before the memory access");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute LDP with post-index writeback");
    expect(state.x[7] == state.x[5] && state.x[8] == state.x[6] &&
           state.x[2] == base + 0xB0U,
           "post-index LDP loads the pair then advances its base");

    // A paired load whose second half crosses the mapping boundary faults atomically.
    write32(memory, base + 0xE0U, 0xA9410440U); // LDP X0,X1,[X2,#16]
    state.program_counter = base + 0xE0U;
    state.x[2] = base + 0xF0U;
    state.x[0] = 0xAAAAU;
    state.x[1] = 0xBBBBU;
    const auto pair_fault_retired = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MemoryFault,
           "report an out-of-range pair load");
    expect(state.x[0] == 0xAAAAU && state.x[1] == 0xBBBBU &&
           state.program_counter == base + 0xE0U &&
           state.instructions_retired == pair_fault_retired,
           "faulting LDP leaves both destination registers and PC unchanged");


    // Unscaled and indexed single-register memory access forms.
    write32(memory, base + 0xE4U, 0xF81F8020U); // STUR X0,[X1,#-8]
    write32(memory, base + 0xE8U, 0xF85F8022U); // LDUR X2,[X1,#-8]
    write32(memory, base + 0xECU, 0xF81F8C23U); // STR X3,[X1,#-8]!
    write32(memory, base + 0xF0U, 0xF8408424U); // LDR X4,[X1],#8
    write32(memory, base + 0xF4U, 0x389FF025U); // LDURSB X5,[X1,#-1]
    state = lucid::cpu::CpuContext{};
    state.program_counter = base + 0xE4U;
    state.x[1] = base + 0xB0U;
    state.x[0] = 0x123456789ABCDEF0ULL;
    state.x[3] = 0x8877665544332211ULL;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute STUR with a signed negative offset");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute LDUR with a signed negative offset");
    expect(state.x[2] == state.x[0],
           "LDUR retrieves the value stored by STUR");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute STR with pre-index writeback");
    expect(state.x[1] == base + 0xA8U,
           "pre-index STR updates the base register");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute LDR with post-index writeback");
    expect(state.x[4] == state.x[3] && state.x[1] == base + 0xB0U,
           "post-index LDR loads before advancing the base register");
    const std::array<std::uint8_t, 1U> signed_byte{0x80U};
    expect(memory.write(base + 0xAFU, signed_byte),
           "write a negative signed-byte load fixture");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute LDURSB with sign extension");
    expect(state.x[5] == 0xFFFFFFFFFFFFFF80ULL,
           "LDURSB sign-extends an 8-bit value to X register width");

    write32(memory, base + 0xF8U, 0xF81F8C23U); // STR X3,[X1,#-8]!
    state.program_counter = base + 0xF8U;
    state.x[1] = base + 4U;
    const auto indexed_fault_base = state.x[1];
    const auto indexed_fault_retired = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MemoryFault,
           "report a pre-index store outside mapped memory");
    expect(state.x[1] == indexed_fault_base &&
           state.program_counter == base + 0xF8U &&
           state.instructions_retired == indexed_fault_retired,
           "faulting pre-index store leaves base and CPU state unchanged");


    // Register-offset addressing: LDR X with LSL #3, LDR W with UXTW #2,
    // and a negative SXTW index.
    write32(memory, base + 0x80U, 0xF8627820U); // LDR X0,[X1,X2,LSL #3]
    write32(memory, base + 0x84U, 0xB8625823U); // LDR W3,[X1,W2,UXTW #2]
    write32(memory, base + 0x88U, 0xF862D824U); // LDR X4,[X1,W2,SXTW #3]
    const std::array<std::uint8_t, 8U> reg_offset_data{
        0x88U, 0x77U, 0x66U, 0x55U, 0x44U, 0x33U, 0x22U, 0x11U
    };
    const std::array<std::uint8_t, 4U> reg_offset_word{
        0x98U, 0xBAU, 0xDCU, 0xFEU
    };
    expect(memory.write(base + 0xB0U, reg_offset_data),
           "seed a register-offset 64-bit load");
    expect(memory.write(base + 0xA8U, reg_offset_word),
           "seed a register-offset 32-bit load");
    expect(memory.write(base + 0xC0U, reg_offset_data),
           "seed a negative register-offset load");
    state = lucid::cpu::CpuContext{};
    state.program_counter = base + 0x80U;
    state.x[1] = base + 0xA0U;
    state.x[2] = 2U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute an LDR using LSL-scaled register offset");
    expect(state.x[0] == 0x1122334455667788ULL,
           "LSL register-offset load selects the scaled 64-bit element");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute an LDR using UXTW-scaled register offset");
    expect(state.x[3] == 0xFEDCBA98U,
           "UXTW register-offset load selects the scaled 32-bit element");
    state.x[1] = base + 0xD0U;
    state.x[2] = static_cast<std::uint64_t>(-2LL);
    state.program_counter = base + 0x88U;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute an LDR using a negative SXTW register offset");
    expect(state.x[4] == 0x1122334455667788ULL,
           "SXTW register-offset load applies a signed negative index");


    // PC-relative literal loads: LDR X, LDR W, LDRSW and PRFM.
    write32(memory, base + 0x10U, 0x58000380U); // LDR X0, [PC,#0x70]
    write32(memory, base + 0x14U, 0x180003A1U); // LDR W1, [PC,#0x74]
    write32(memory, base + 0x18U, 0x980003A2U); // LDRSW X2, [PC,#0x74]
    write32(memory, base + 0x1CU, 0xD80003A3U); // PRFM [PC,#0x74]
    const std::array<std::uint8_t, 8U> literal_x{
        0x88U, 0x77U, 0x66U, 0x55U, 0x44U, 0x33U, 0x22U, 0x11U
    };
    const std::array<std::uint8_t, 4U> literal_w{
        0xDDU, 0xCCU, 0xBBU, 0xAAU
    };
    const std::array<std::uint8_t, 4U> literal_signed{
        0x80U, 0xFFU, 0xFFU, 0xFFU
    };
    expect(memory.write(base + 0x80U, literal_x),
           "seed a 64-bit PC-relative literal");
    expect(memory.write(base + 0x88U, literal_w),
           "seed a 32-bit PC-relative literal");
    expect(memory.write(base + 0x8CU, literal_signed),
           "seed a sign-extended PC-relative literal");
    state = lucid::cpu::CpuContext{};
    state.program_counter = base + 0x10U;
    for (unsigned index = 0U; index < 4U; ++index) {
        expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
               "execute an A64 literal load or prefetch hint");
    }
    expect(state.x[0] == 0x1122334455667788ULL,
           "LDR X loads a 64-bit PC-relative literal");
    expect(state.x[1] == 0xAABBCCDDU,
           "LDR W zero-extends a 32-bit PC-relative literal");
    expect(state.x[2] == 0xFFFFFFFFFFFFFF80ULL,
           "LDRSW sign-extends a 32-bit PC-relative literal");
    expect(state.instructions_retired == 4U,
           "PRFM literal retires as a non-memory hint");

    write32(memory, base + 0x20U, 0x587FFFE3U); // LDR X3, far outside mapping.
    state.program_counter = base + 0x20U;
    const auto literal_fault_retired = state.instructions_retired;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::MemoryFault,
           "report a PC-relative literal load outside mapped memory");
    expect(state.program_counter == base + 0x20U &&
           state.instructions_retired == literal_fault_retired,
           "faulting literal load leaves CPU state unchanged");


    // ADC/ADCS and SBC/SBCS consume and update NZCV carry semantics.
    write32(memory, base + 0x28U, 0x9A020020U); // ADC X0,X1,X2
    write32(memory, base + 0x2CU, 0xBA050083U); // ADCS X3,X4,X5
    write32(memory, base + 0x30U, 0xDA0800E6U); // SBC X6,X7,X8
    write32(memory, base + 0x34U, 0xFA0B0149U); // SBCS X9,X10,X11
    state = lucid::cpu::CpuContext{};
    state.program_counter = base + 0x28U;
    state.x[1] = 5U;
    state.x[2] = 7U;
    state.x[4] = 0x7FFFFFFFFFFFFFFFULL;
    state.x[5] = 0U;
    state.x[7] = 10U;
    state.x[8] = 3U;
    state.x[10] = 3U;
    state.x[11] = 5U;
    state.flags = {true, false, true, true};
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute ADC with carry-in");
    expect(state.x[0] == 13U && state.flags.negative &&
           !state.flags.zero && state.flags.carry && state.flags.overflow,
           "ADC consumes carry-in without changing NZCV flags");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute ADCS and update NZCV flags");
    expect(state.x[3] == 0x8000000000000000ULL &&
           state.flags.negative && !state.flags.zero &&
           !state.flags.carry && state.flags.overflow,
           "ADCS reports signed overflow when adding carry to max positive");
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute SBC with borrow-in");
    expect(state.x[6] == 6U && state.flags.negative &&
           !state.flags.carry && state.flags.overflow,
           "SBC consumes inverted carry as borrow without changing flags");
    state.flags.carry = true;
    expect(cpu.step(state, memory) == lucid::cpu::StepResult::Executed,
           "execute SBCS without borrow-in");
    expect(state.x[9] == 0xFFFFFFFFFFFFFFFEULL &&
           state.flags.negative && !state.flags.zero &&
           !state.flags.carry && !state.flags.overflow,
           "SBCS updates negative, carry and overflow for a negative result");


    // Initial Advanced SIMD/NEON integer vector operations.
    constexpr std::uint64_t simd_base = 0x4000U;
    lucid::core::MemoryBus simd_memory;
    expect(simd_memory.map_region(simd_base, 0x100U),
           "map isolated Advanced SIMD instruction memory");
    write32(simd_memory, simd_base + 0x00U, 0x4E228420U); // ADD V0.16B,V1.16B,V2.16B
    write32(simd_memory, simd_base + 0x04U, 0x6E658483U); // SUB V3.8H,V4.8H,V5.8H
    write32(simd_memory, simd_base + 0x08U, 0x4EA884E6U); // ADD V6.4S,V7.4S,V8.4S
    write32(simd_memory, simd_base + 0x0CU, 0x4EEB8549U); // ADD V9.2D,V10.2D,V11.2D
    write32(simd_memory, simd_base + 0x10U, 0x4E2E1DACU); // AND V12.16B,V13.16B,V14.16B
    write32(simd_memory, simd_base + 0x14U, 0x0EB11E0FU); // ORR V15.8B,V16.8B,V17.8B
    write32(simd_memory, simd_base + 0x18U, 0x6E341E72U); // EOR V18.16B,V19.16B,V20.16B
    write32(simd_memory, simd_base + 0x1CU, 0x0E3786D5U); // ADD V21.8B,V22.8B,V23.8B
    write32(simd_memory, simd_base + 0x20U, 0x0EE28420U); // Reserved 1D arrangement (Q=0)

    lucid::cpu::CpuContext simd_state{};
    simd_state.program_counter = simd_base;
    simd_state.v[1] = {0xFFFFFFFFFFFFFFFFULL, 0x0101010101010101ULL};
    simd_state.v[2] = {0x0101010101010101ULL, 0x0101010101010101ULL};
    simd_state.v[4] = {0x0001000100010001ULL, 0x0001000100010001ULL};
    simd_state.v[5] = {0x0002000200020002ULL, 0x0002000200020002ULL};
    simd_state.v[7] = {0x0000000100000002ULL, 0x0000000300000004ULL};
    simd_state.v[8] = {0x0000000500000006ULL, 0x0000000700000008ULL};
    simd_state.v[10] = {0xFFFFFFFFFFFFFFFFULL, 1ULL};
    simd_state.v[11] = {1ULL, 2ULL};
    simd_state.v[13] = {0xFFFF0000FFFF0000ULL, 0xAAAAAAAA55555555ULL};
    simd_state.v[14] = {0x0F0F0F0F0F0F0F0FULL, 0xFF00FF00FF00FF00ULL};
    simd_state.v[16] = {0x0F0F0F0F0F0F0F0FULL, 0x123456789ABCDEF0ULL};
    simd_state.v[17] = {0xF0F0F0F0F0F0F0F0ULL, 0xFFFFFFFFFFFFFFFFULL};
    simd_state.v[19] = {0xAAAAAAAAAAAAAAAAULL, 0x0123456789ABCDEFULL};
    simd_state.v[20] = {0xFFFF0000FFFF0000ULL, 0x1111111111111111ULL};
    simd_state.v[22] = {0xFF00FF00FF00FF00ULL, 0xFFFFFFFFFFFFFFFFULL};
    simd_state.v[23] = {0x0101010101010101ULL, 0xFFFFFFFFFFFFFFFFULL};
    simd_state.flags = {true, false, true, true};

    lucid::cpu::A64Interpreter simd_cpu;
    for (unsigned index = 0U; index < 8U; ++index) {
        expect(simd_cpu.step(simd_state, simd_memory) ==
                   lucid::cpu::StepResult::Executed,
               "execute a supported Advanced SIMD vector instruction");
    }
    expect(simd_state.v[0].low == 0U &&
           simd_state.v[0].high == 0x0202020202020202ULL,
           "ADD vector wraps each byte lane independently across 128 bits");
    expect(simd_state.v[3].low == 0xFFFFFFFFFFFFFFFFULL &&
           simd_state.v[3].high == 0xFFFFFFFFFFFFFFFFULL,
           "SUB vector wraps each halfword lane independently");
    expect(simd_state.v[6].low == 0x0000000600000008ULL &&
           simd_state.v[6].high == 0x0000000A0000000CULL,
           "ADD vector computes independent 32-bit lanes");
    expect(simd_state.v[9].low == 0U && simd_state.v[9].high == 3U,
           "ADD vector supports two 64-bit lanes");
    expect(simd_state.v[12].low == 0x0F0F00000F0F0000ULL &&
           simd_state.v[12].high == 0xAA00AA0055005500ULL,
           "AND vector combines both 64-bit halves");
    expect(simd_state.v[15].low == 0xFFFFFFFFFFFFFFFFULL &&
           simd_state.v[15].high == 0U,
           "ORR 64-bit vector form clears the upper destination half");
    expect(simd_state.v[18].low == 0x5555AAAA5555AAAAULL &&
           simd_state.v[18].high == 0x1032547698BADCFEULL,
           "EOR vector combines all 128 register bits");
    expect(simd_state.v[21].low == 0x0001000100010001ULL &&
           simd_state.v[21].high == 0U,
           "ADD 64-bit vector form wraps lanes and clears upper bits");
    expect(simd_state.flags.negative && !simd_state.flags.zero &&
           simd_state.flags.carry && simd_state.flags.overflow,
           "Advanced SIMD integer operations leave NZCV flags unchanged");

    const auto simd_unsupported_pc = simd_state.program_counter;
    const auto simd_unsupported_retired = simd_state.instructions_retired;
    const auto simd_unsupported_destination = simd_state.v[0];
    expect(simd_cpu.step(simd_state, simd_memory) ==
               lucid::cpu::StepResult::UnsupportedInstruction,
           "reject reserved 64-bit-only vector arrangement in Q=0 form");
    expect(simd_state.program_counter == simd_unsupported_pc &&
           simd_state.instructions_retired == simd_unsupported_retired &&
           simd_state.v[0].low == simd_unsupported_destination.low &&
           simd_state.v[0].high == simd_unsupported_destination.high,
           "unsupported Advanced SIMD encoding leaves CPU state unchanged");


    // Exclusive load/store reservations shared by cores through MemoryBus.
    constexpr std::uint64_t exclusive_code = 0x7000U;
    constexpr std::uint64_t exclusive_data = 0x8000U;
    lucid::core::MemoryBus exclusive_memory;
    expect(exclusive_memory.map_region(exclusive_code, 0x100U),
           "map exclusive-access instruction fixture");
    expect(exclusive_memory.map_region(exclusive_data, 0x100U),
           "map shared data for exclusive-access fixture");
    write32(exclusive_memory, exclusive_code + 0x00U, 0x885F7C20U); // LDXR W0,[X1]
    write32(exclusive_memory, exclusive_code + 0x04U, 0x88027C23U); // STXR W2,W3,[X1]
    write32(exclusive_memory, exclusive_code + 0x08U, 0xC85FFC24U); // LDAXR X4,[X1]
    write32(exclusive_memory, exclusive_code + 0x0CU, 0xC805FC26U); // STLXR W5,X6,[X1]
    write32(exclusive_memory, exclusive_code + 0x10U, 0xC85F7C27U); // LDXR X7,[X1]
    write32(exclusive_memory, exclusive_code + 0x14U, 0xC8087C29U); // STXR W8,X9,[X1]
    write32(exclusive_memory, exclusive_code + 0x18U, 0xD5033F5FU); // CLREX
    write32(exclusive_memory, exclusive_code + 0x1CU, 0x880B7C2CU); // STXR W11,W12,[X1]

    const auto store_u64 = [&exclusive_memory](const std::uint64_t value) {
        std::array<std::uint8_t, 8U> bytes{};
        for (std::size_t byte = 0U; byte < bytes.size(); ++byte)
            bytes[byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU);
        return exclusive_memory.write(exclusive_data, bytes);
    };
    const auto read_u64 = [&exclusive_memory]() {
        std::array<std::uint8_t, 8U> bytes{};
        expect(exclusive_memory.read(exclusive_data, bytes),
               "read shared word after exclusive operation");
        std::uint64_t value = 0U;
        for (std::size_t byte = 0U; byte < bytes.size(); ++byte)
            value |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
        return value;
    };

    lucid::cpu::CpuContext exclusive_state{};
    exclusive_state.program_counter = exclusive_code;
    exclusive_state.x[1] = exclusive_data;
    exclusive_state.x[3] = 9U;
    expect(store_u64(7U), "seed exclusive-access data word");
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute LDXR W and establish reservation");
    expect(exclusive_state.x[0] == 7U &&
           exclusive_state.exclusive_monitor_valid &&
           exclusive_state.exclusive_size == 4U,
           "LDXR reads a word and records a local reservation");
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute STXR W while the reservation remains valid");
    expect(exclusive_state.x[2] == 0U &&
           !exclusive_state.exclusive_monitor_valid &&
           (read_u64() & 0xFFFFFFFFULL) == 9U,
           "successful STXR stores the word and returns status zero");

    expect(store_u64(11U), "reset data before testing reservation invalidation");
    exclusive_state = lucid::cpu::CpuContext{};
    exclusive_state.program_counter = exclusive_code;
    exclusive_state.x[1] = exclusive_data;
    exclusive_state.x[3] = 33U;
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "establish a reservation before an inter-core write");
    const std::array<std::uint8_t, 8U> peer_value{
        0x15U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    expect(exclusive_memory.write(exclusive_data, peer_value),
           "simulate a peer-core normal store to the reserved granule");
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute STXR after a peer-core write invalidates the reservation");
    expect(exclusive_state.x[2] == 1U &&
           (read_u64() & 0xFFFFFFFFULL) == 21U,
           "failed STXR reports status one and preserves the peer's value");

    expect(store_u64(0x1122334455667788ULL),
           "seed a 64-bit acquire/release exclusive operation");
    exclusive_state = lucid::cpu::CpuContext{};
    exclusive_state.program_counter = exclusive_code + 0x08U;
    exclusive_state.x[1] = exclusive_data;
    exclusive_state.x[6] = 0x8877665544332211ULL;
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute LDAXR X acquire-exclusive load");
    expect(exclusive_state.x[4] == 0x1122334455667788ULL &&
           exclusive_state.exclusive_monitor_valid &&
           exclusive_state.exclusive_size == 8U,
           "LDAXR reads a doubleword and retains its reservation");
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute STLXR X release-exclusive store");
    expect(exclusive_state.x[5] == 0U &&
           read_u64() == 0x8877665544332211ULL,
           "successful STLXR writes a doubleword and returns status zero");

    exclusive_state = lucid::cpu::CpuContext{};
    exclusive_state.program_counter = exclusive_code + 0x10U;
    exclusive_state.x[1] = exclusive_data;
    exclusive_state.x[9] = 0xAABBCCDDEEFF0011ULL;
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute 64-bit LDXR before CLREX");
    exclusive_state.program_counter = exclusive_code + 0x18U;
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute CLREX and discard the reservation");
    exclusive_state.program_counter = exclusive_code + 0x1CU;
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute STXR after CLREX");
    expect(exclusive_state.x[11] == 1U &&
           read_u64() == 0x8877665544332211ULL,
           "STXR after CLREX fails without modifying memory");

    exclusive_state = lucid::cpu::CpuContext{};
    exclusive_state.program_counter = exclusive_code;
    exclusive_state.x[1] = exclusive_data + 1U;
    const auto exclusive_fault_pc = exclusive_state.program_counter;
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::MemoryFault,
           "reject a misaligned exclusive load");
    expect(exclusive_state.program_counter == exclusive_fault_pc &&
           !exclusive_state.exclusive_monitor_valid &&
           exclusive_state.instructions_retired == 0U,
           "misaligned exclusive load leaves CPU state unchanged");

    // DMB/DSB/ISB complete as sequencing points in this synchronous
    // interpreter: memory writes are immediate and each step fetches anew.
    constexpr std::uint64_t barrier_code = exclusive_code + 0x20U;
    write32(exclusive_memory, barrier_code + 0x00U, 0xD5033FBFU); // DMB SY
    write32(exclusive_memory, barrier_code + 0x04U, 0xD5033F9FU); // DSB SY
    write32(exclusive_memory, barrier_code + 0x08U, 0xD5033FDFU); // ISB SY
    exclusive_state = lucid::cpu::CpuContext{};
    exclusive_state.program_counter = barrier_code;
    const auto retired_before_barriers = exclusive_state.instructions_retired;
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute DMB SY");
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute DSB SY");
    expect(cpu.step(exclusive_state, exclusive_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute ISB SY");
    expect(exclusive_state.program_counter == barrier_code + 12U &&
           exclusive_state.instructions_retired == retired_before_barriers + 3U,
           "barriers retire in order and advance the instruction stream");

    // SIMD/FP register-pair memory operations (S/D/Q forms).
    constexpr std::uint64_t pair_code = 0x6000U;
    constexpr std::uint64_t pair_data = 0x6100U;
    lucid::core::MemoryBus pair_memory;
    expect(pair_memory.map_region(pair_code, 0x100U),
           "map vector pair instruction fixture");
    expect(pair_memory.map_region(pair_data, 0x100U),
           "map vector pair data fixture");
    write32(pair_memory, pair_code + 0x00U, 0x2DBF0440U); // STP S0,S1,[X2,#-8]!
    write32(pair_memory, pair_code + 0x04U, 0x2CC10C42U); // LDP S2,S3,[X2],#8
    write32(pair_memory, pair_code + 0x08U, 0x6D011444U); // STP D4,D5,[X2,#16]
    write32(pair_memory, pair_code + 0x0CU, 0xAD7F1C46U); // LDP Q6,Q7,[X2,#-32]
    lucid::cpu::CpuContext pair_state{};
    pair_state.program_counter = pair_code;
    pair_state.x[2] = pair_data + 8U;
    pair_state.v[0] = {0xAABBCCDDU, 0x1111222233334444ULL};
    pair_state.v[1] = {0x55667788U, 0x5555666677778888ULL};
    expect(cpu.step(pair_state, pair_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute pre-index SIMD S-register pair store");
    expect(pair_state.x[2] == pair_data,
           "SIMD S pair pre-index updates the base register");
    pair_state.program_counter = pair_code + 4U;
    expect(cpu.step(pair_state, pair_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute post-index SIMD S-register pair load");
    expect(pair_state.v[2].low == 0xAABBCCDDU && pair_state.v[2].high == 0U &&
           pair_state.v[3].low == 0x55667788U && pair_state.v[3].high == 0U &&
           pair_state.x[2] == pair_data + 8U,
           "SIMD S pair loads zero upper bits and applies post-index writeback");

    pair_state.program_counter = pair_code + 8U;
    pair_state.x[2] = pair_data;
    pair_state.v[4] = {0x0123456789ABCDEFULL, 0xFFFFFFFFFFFFFFFFULL};
    pair_state.v[5] = {0xFEDCBA9876543210ULL, 0xAAAAAAAAAAAAAAAAULL};
    expect(cpu.step(pair_state, pair_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute signed-offset SIMD D-register pair store");
    std::array<std::uint8_t, 16U> stored_d{};
    expect(pair_memory.read(pair_data + 16U, stored_d),
           "read back SIMD D pair store");
    expect(stored_d[0] == 0xEFU && stored_d[7] == 0x01U &&
           stored_d[8] == 0x10U && stored_d[15] == 0xFEU,
           "SIMD D pair stores both 64-bit register payloads");

    pair_state.program_counter = pair_code + 12U;
    pair_state.x[2] = pair_data + 32U;
    expect(cpu.step(pair_state, pair_memory) ==
               lucid::cpu::StepResult::Executed,
           "execute signed-offset SIMD Q-register pair load");
    expect(pair_state.v[6].low == 0x55667788AABBCCDDULL &&
           pair_state.v[6].high == 0U &&
           pair_state.v[7].low == 0x0123456789ABCDEFULL &&
           pair_state.v[7].high == 0xFEDCBA9876543210ULL,
           "SIMD Q pair loads all 128 bits from its 32-byte memory range");

    // Unsigned-offset SIMD/FP single-register LDR/STR S, D and Q forms.
    write32(pair_memory, pair_code + 0x10U, 0xBD000420U); // STR S0,[X1,#4]
    write32(pair_memory, pair_code + 0x14U, 0xBD400422U); // LDR S2,[X1,#4]
    write32(pair_memory, pair_code + 0x18U, 0xFD000423U); // STR D3,[X1,#8]
    write32(pair_memory, pair_code + 0x1CU, 0xFD400424U); // LDR D4,[X1,#8]
    write32(pair_memory, pair_code + 0x20U, 0x3D800425U); // STR Q5,[X1,#16]
    write32(pair_memory, pair_code + 0x24U, 0x3DC00426U); // LDR Q6,[X1,#16]
    pair_state.x[1] = pair_data + 0x40U;
    pair_state.v[0] = {0xFFFFFFFFAABBCCDDULL, 0x1111222233334444ULL};
    pair_state.program_counter = pair_code + 0x10U;
    expect(cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed &&
           cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed,
           "execute unsigned-offset STR/LDR S-register");
    expect(pair_state.v[2].low == 0xAABBCCDDU && pair_state.v[2].high == 0U,
           "LDR S zeroes bits above its 32-bit payload");
    pair_state.v[3] = {0x0123456789ABCDEFULL, 0xAAAAAAAAAAAAAAAAULL};
    pair_state.program_counter = pair_code + 0x18U;
    expect(cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed &&
           cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed,
           "execute unsigned-offset STR/LDR D-register");
    expect(pair_state.v[4].low == 0x0123456789ABCDEFULL &&
           pair_state.v[4].high == 0U,
           "LDR D zeroes bits above its 64-bit payload");
    pair_state.v[5] = {0x8877665544332211ULL, 0xFFEEDDCCBBAA0099ULL};
    pair_state.program_counter = pair_code + 0x20U;
    expect(cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed &&
           cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed,
           "execute unsigned-offset STR/LDR Q-register");
    expect(pair_state.v[6].low == 0x8877665544332211ULL &&
           pair_state.v[6].high == 0xFFEEDDCCBBAA0099ULL,
           "LDR Q restores the complete 128-bit SIMD register");

    // LDAR/STLR aligned word and doubleword acquire/release operations.
    write32(pair_memory, pair_code + 0x28U, 0x889FFC62U); // STLR W2,[X3]
    write32(pair_memory, pair_code + 0x2CU, 0x88DFFC20U); // LDAR W0,[X1]
    write32(pair_memory, pair_code + 0x30U, 0xC89FFCE6U); // STLR X6,[X7]
    write32(pair_memory, pair_code + 0x34U, 0xC8DFFCA4U); // LDAR X4,[X5]
    const std::uint64_t atomic_data = pair_data + 0x80U;
    pair_state = lucid::cpu::CpuContext{};
    pair_state.x[1] = atomic_data;
    pair_state.x[2] = 0xA1B2C3D4U;
    pair_state.x[3] = atomic_data;
    pair_state.program_counter = pair_code + 0x28U;
    expect(cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed &&
           cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed,
           "execute STLR W followed by LDAR W");
    expect(pair_state.x[0] == 0xA1B2C3D4U,
           "LDAR W observes the preceding release store");
    pair_state.x[5] = atomic_data + 8U;
    pair_state.x[6] = 0x1122334455667788ULL;
    pair_state.x[7] = atomic_data + 8U;
    pair_state.program_counter = pair_code + 0x30U;
    expect(cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed &&
           cpu.step(pair_state, pair_memory) == lucid::cpu::StepResult::Executed,
           "execute STLR X followed by LDAR X");
    expect(pair_state.x[4] == 0x1122334455667788ULL,
           "LDAR X observes the preceding release store");

    // FEAT_LSE atomic instructions, with encodings checked by an AArch64 assembler.
    write32(pair_memory,pair_code+0x38U,0x38200041U); // LDADDB W0,W1,[X2]
    write32(pair_memory,pair_code+0x3CU,0x78A300A4U); // LDADDAH W3,W4,[X5]
    write32(pair_memory,pair_code+0x40U,0xB8260107U); // LDADD W6,W7,[X8]
    write32(pair_memory,pair_code+0x44U,0xF8E9016AU); // LDADDAL X9,X10,[X11]
    write32(pair_memory,pair_code+0x48U,0x382C11CDU); // LDCLRB W12,W13,[X14]
    write32(pair_memory,pair_code+0x4CU,0x782F2230U); // LDEORH W15,W16,[X17]
    write32(pair_memory,pair_code+0x50U,0xF8323293U); // LDSET X18,X19,[X20]
    write32(pair_memory,pair_code+0x54U,0xF83582F6U); // SWP X21,X22,[X23]
    write32(pair_memory,pair_code+0x58U,0xB8204041U); // LDSMAX W0,W1,[X2]
    write32(pair_memory,pair_code+0x5CU,0xF82350A4U); // LDSMIN X3,X4,[X5]
    write32(pair_memory,pair_code+0x60U,0xB8266107U); // LDUMAX W6,W7,[X8]
    write32(pair_memory,pair_code+0x64U,0xF829716AU); // LDUMIN X9,X10,[X11]

    auto write_atomic_value=[&](const std::uint64_t address,const std::size_t size,
                                const std::uint64_t value){
        std::array<std::uint8_t,8U> bytes{};
        for(std::size_t byte=0U;byte<size;++byte)
            bytes[byte]=static_cast<std::uint8_t>((value>>(byte*8U))&0xFFU);
        expect(pair_memory.write(address,
            std::span<const std::uint8_t>(bytes.data(),size)),
            "initialize LSE atomic memory operand");
    };
    auto read_atomic_value=[&](const std::uint64_t address,const std::size_t size){
        std::array<std::uint8_t,8U> bytes{};
        expect(pair_memory.read(address,
            std::span<std::uint8_t>(bytes.data(),size)),
            "read LSE atomic memory result");
        std::uint64_t value=0U;
        for(std::size_t byte=0U;byte<size;++byte)
            value|=static_cast<std::uint64_t>(bytes[byte])<<(byte*8U);
        return value;
    };
    const std::uint64_t lse_data=pair_data+0x80U;
    pair_state=lucid::cpu::CpuContext{};
    write_atomic_value(lse_data,1U,0x10U);
    pair_state.x[0]=5U;pair_state.x[2]=lse_data;
    pair_state.program_counter=pair_code+0x38U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[1]==0x10U&&read_atomic_value(lse_data,1U)==0x15U,
        "LDADDB returns the old byte and stores the wrapping sum");

    write_atomic_value(lse_data+2U,2U,0x1000U);
    pair_state.x[3]=0x30U;pair_state.x[5]=lse_data+2U;
    pair_state.program_counter=pair_code+0x3CU;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[4]==0x1000U&&read_atomic_value(lse_data+2U,2U)==0x1030U,
        "LDADDAH atomically adds a halfword and returns the prior value");

    write_atomic_value(lse_data+4U,4U,0xFFFFFFFEU);
    pair_state.x[6]=5U;pair_state.x[8]=lse_data+4U;
    pair_state.program_counter=pair_code+0x40U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[7]==0xFFFFFFFEU&&read_atomic_value(lse_data+4U,4U)==3U,
        "LDADD wraps a 32-bit word result");

    write_atomic_value(lse_data+8U,8U,100U);
    pair_state.x[9]=3U;pair_state.x[11]=lse_data+8U;
    pair_state.program_counter=pair_code+0x44U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[10]==100U&&read_atomic_value(lse_data+8U,8U)==103U,
        "LDADDAL atomically adds a doubleword");

    write_atomic_value(lse_data+16U,1U,0xFFU);
    pair_state.x[12]=0x0FU;pair_state.x[14]=lse_data+16U;
    pair_state.program_counter=pair_code+0x48U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[13]==0xFFU&&read_atomic_value(lse_data+16U,1U)==0xF0U,
        "LDCLRB clears selected bits atomically");

    write_atomic_value(lse_data+18U,2U,0xAAAAU);
    pair_state.x[15]=0x00FFU;pair_state.x[17]=lse_data+18U;
    pair_state.program_counter=pair_code+0x4CU;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[16]==0xAAAAU&&read_atomic_value(lse_data+18U,2U)==0xAA55U,
        "LDEORH atomically XORs a halfword");

    write_atomic_value(lse_data+24U,8U,0x1000U);
    pair_state.x[18]=0x0FU;pair_state.x[20]=lse_data+24U;
    pair_state.program_counter=pair_code+0x50U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[19]==0x1000U&&read_atomic_value(lse_data+24U,8U)==0x100FU,
        "LDSET atomically ORs a doubleword");

    write_atomic_value(lse_data+32U,8U,0x3333U);
    pair_state.x[21]=0x4444U;pair_state.x[23]=lse_data+32U;
    pair_state.program_counter=pair_code+0x54U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[22]==0x3333U&&read_atomic_value(lse_data+32U,8U)==0x4444U,
        "SWP exchanges a register and doubleword atomically");

    write_atomic_value(lse_data+40U,4U,0xFFFFFFFBU);
    pair_state.x[0]=3U;pair_state.x[2]=lse_data+40U;
    pair_state.program_counter=pair_code+0x58U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[1]==0xFFFFFFFBU&&read_atomic_value(lse_data+40U,4U)==3U,
        "LDSMAX compares signed 32-bit values");

    write_atomic_value(lse_data+48U,8U,5U);
    pair_state.x[3]=0xFFFFFFFFFFFFFFFEULL;pair_state.x[5]=lse_data+48U;
    pair_state.program_counter=pair_code+0x5CU;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[4]==5U&&read_atomic_value(lse_data+48U,8U)==0xFFFFFFFFFFFFFFFEULL,
        "LDSMIN compares signed 64-bit values");

    write_atomic_value(lse_data+56U,4U,0x1000U);
    pair_state.x[6]=0x2000U;pair_state.x[8]=lse_data+56U;
    pair_state.program_counter=pair_code+0x60U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[7]==0x1000U&&read_atomic_value(lse_data+56U,4U)==0x2000U,
        "LDUMAX compares unsigned 32-bit values");

    write_atomic_value(lse_data+64U,8U,0x100U);
    pair_state.x[9]=5U;pair_state.x[11]=lse_data+64U;
    pair_state.program_counter=pair_code+0x64U;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::Executed&&
           pair_state.x[10]==0x100U&&read_atomic_value(lse_data+64U,8U)==5U,
        "LDUMIN compares unsigned 64-bit values");

    pair_state.program_counter=pair_code+0x44U;
    pair_state.x[11]=lse_data+9U;
    const auto misaligned_before=pair_state;
    expect(cpu.step(pair_state,pair_memory)==lucid::cpu::StepResult::MemoryFault&&
           pair_state.program_counter==misaligned_before.program_counter&&
           pair_state.x==misaligned_before.x,
        "reject misaligned LSE atomic operands without changing CPU state");

    const std::uint64_t counter_address=pair_data+0xF0U;
    write_atomic_value(counter_address,8U,0U);
    std::atomic<bool> workers_ok{true};
    std::array<std::thread,8U> atomic_workers{};
    for(auto& worker:atomic_workers){
        worker=std::thread([&]{
            for(unsigned iteration=0U;iteration<250U;++iteration){
                std::uint64_t previous=0U;
                if(!pair_memory.atomic_rmw(counter_address,8U,1U,
                    lucid::core::AtomicOperation::Add,previous))
                    workers_ok.store(false,std::memory_order_relaxed);
            }
        });
    }
    for(auto& worker:atomic_workers)worker.join();
    expect(workers_ok.load(std::memory_order_relaxed)&&
           read_atomic_value(counter_address,8U)==2000U,
        "serialize 2,000 concurrent atomic increments without lost updates");

    std::uint64_t reservation=0U;
    std::array<std::uint8_t,8U> reservation_bytes{};
    expect(pair_memory.read_exclusive(counter_address,reservation_bytes,reservation),
        "capture reservation version before an LSE atomic update");
    std::uint64_t old_counter=0U;
    expect(pair_memory.atomic_rmw(counter_address,8U,1U,
        lucid::core::AtomicOperation::Add,old_counter),
        "perform LSE atomic update against a reserved granule");
    bool exclusive_stored=true;
    expect(pair_memory.write_exclusive(counter_address,reservation_bytes,
        reservation,exclusive_stored)&&!exclusive_stored,
        "LSE atomic update invalidates a peer's exclusive reservation");

    if (failures != 0) {
        std::cerr << failures << " A64 test(s) failed.\n";
        return 1;
    }
    std::cout << "All A64 subset tests passed.\n";
    return 0;
}
