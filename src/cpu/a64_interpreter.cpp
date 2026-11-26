#include "lucid/cpu/cpu_backend.hpp"
#include "lucid/arch/system_registers.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace lucid::cpu {
namespace {

[[nodiscard]] std::int64_t sign_extend(const std::uint64_t value,
                                       const unsigned bits) noexcept {
    const std::uint64_t sign = std::uint64_t{1} << (bits - 1U);
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1U;
    const std::uint64_t narrowed = value & mask;
    return static_cast<std::int64_t>((narrowed ^ sign) - sign);
}

[[nodiscard]] std::uint64_t width_mask(const bool is_64_bit) noexcept {
    return is_64_bit ? std::numeric_limits<std::uint64_t>::max()
                     : 0xFFFFFFFFULL;
}

[[nodiscard]] std::uint64_t read_register(const CpuContext& context,
                                          const unsigned index) noexcept {
    return index == 31U ? 0U : context.x[index];
}

void write_register(CpuContext& context, unsigned index,
                    std::uint64_t value, bool is_64_bit) noexcept;

[[nodiscard]] std::uint64_t read_addsub_register(
    const CpuContext& context, unsigned index,
    bool is_64_bit, bool set_flags) noexcept {
    if (index == 31U && !set_flags) return context.sp & width_mask(is_64_bit);
    return read_register(context, index) & width_mask(is_64_bit);
}
void write_addsub_register(CpuContext& context, unsigned index,
    std::uint64_t value, bool is_64_bit, bool set_flags) noexcept {
    if (index == 31U && !set_flags) {
        context.sp = is_64_bit ? value : (value & 0xFFFFFFFFULL);
        return;
    }
    write_register(context, index, value, is_64_bit);
}
[[nodiscard]] bool shifted_operand(std::uint64_t input, unsigned kind,
    unsigned amount, bool is_64_bit, std::uint64_t& output) noexcept {
    const unsigned width = is_64_bit ? 64U : 32U;
    if (amount >= width || kind > 3U) return false;
    const std::uint64_t mask = width_mask(is_64_bit);
    const std::uint64_t value = input & mask;
    switch (kind) {
    case 0U: output = amount == 0U ? value : (value << amount) & mask; return true;
    case 1U: output = value >> amount; return true;
    case 2U:
        output = static_cast<std::uint64_t>(sign_extend(value, width) >> amount) & mask;
        return true;
    case 3U:
        output = amount == 0U ? value :
            ((value >> amount) | (value << (width - amount))) & mask;
        return true;
    default: return false;
    }
}
void set_logic_flags(CpuContext& context, std::uint64_t result,
                     bool is_64_bit) noexcept {
    const std::uint64_t value = result & width_mask(is_64_bit);
    context.flags.negative = ((value >> (is_64_bit ? 63U : 31U)) & 1U) != 0U;
    context.flags.zero = value == 0U;
    context.flags.carry = false;
    context.flags.overflow = false;
}

[[nodiscard]] std::uint64_t read_base_register(const CpuContext& context,
                                               const unsigned index) noexcept {
    return index == 31U ? context.sp : context.x[index];
}

void write_register(CpuContext& context, const unsigned index,
                    const std::uint64_t value, const bool is_64_bit) noexcept {
    if (index == 31U) {
        return;
    }
    context.x[index] = is_64_bit ? value : (value & 0xFFFFFFFFULL);
}

void set_add_flags(CpuContext& context, const std::uint64_t left,
                   const std::uint64_t right, const std::uint64_t result,
                   const bool is_64_bit) noexcept {
    const unsigned sign_bit = is_64_bit ? 63U : 31U;
    const std::uint64_t mask = width_mask(is_64_bit);
    const std::uint64_t a = left & mask;
    const std::uint64_t b = right & mask;
    const std::uint64_t r = result & mask;
    context.flags.negative = ((r >> sign_bit) & 1U) != 0U;
    context.flags.zero = r == 0U;
    if (is_64_bit) {
        context.flags.carry = r < a;
    } else {
        context.flags.carry = (a + b) > mask;
    }
    context.flags.overflow =
        (((~(a ^ b) & (a ^ r)) >> sign_bit) & 1U) != 0U;
}

void set_sub_flags(CpuContext& context, const std::uint64_t left,
                   const std::uint64_t right, const std::uint64_t result,
                   const bool is_64_bit) noexcept {
    const unsigned sign_bit = is_64_bit ? 63U : 31U;
    const std::uint64_t mask = width_mask(is_64_bit);
    const std::uint64_t a = left & mask;
    const std::uint64_t b = right & mask;
    const std::uint64_t r = result & mask;
    context.flags.negative = ((r >> sign_bit) & 1U) != 0U;
    context.flags.zero = r == 0U;
    context.flags.carry = a >= b;
    context.flags.overflow =
        ((((a ^ b) & (a ^ r)) >> sign_bit) & 1U) != 0U;
}

[[nodiscard]] bool condition_holds(const CpuFlags& flags,
                                   const unsigned condition) noexcept {
    switch (condition) {
    case 0U: return flags.zero;                         // EQ
    case 1U: return !flags.zero;                        // NE
    case 2U: return flags.carry;                        // CS/HS
    case 3U: return !flags.carry;                       // CC/LO
    case 4U: return flags.negative;                     // MI
    case 5U: return !flags.negative;                    // PL
    case 6U: return flags.overflow;                     // VS
    case 7U: return !flags.overflow;                    // VC
    case 8U: return flags.carry && !flags.zero;          // HI
    case 9U: return !flags.carry || flags.zero;          // LS
    case 10U: return flags.negative == flags.overflow;   // GE
    case 11U: return flags.negative != flags.overflow;   // LT
    case 12U: return !flags.zero &&
                     (flags.negative == flags.overflow); // GT
    case 13U: return flags.zero ||
                     (flags.negative != flags.overflow); // LE
    case 14U: return true;                              // AL
    default: return false;                             // NV/reserved
    }
}

[[nodiscard]] bool fetch_instruction(CpuMemory& memory,
                                     const std::uint64_t pc,
                                     std::uint32_t& instruction) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read_instruction(pc, bytes)) {
        return false;
    }

    instruction = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

} // namespace

StepResult A64Interpreter::step(CpuContext& context, core::MemoryBus& memory) {
    CpuMemory cpu_memory(memory);
    return step(context, cpu_memory);
}

StepResult A64Interpreter::step(CpuContext& context, CpuMemory& memory) {
    if ((context.program_counter & 3U) != 0U) {
        return StepResult::MisalignedProgramCounter;
    }

    std::uint32_t instruction = 0U;
    if (!fetch_instruction(memory, context.program_counter, instruction)) {
        return StepResult::MemoryFault;
    }

    const std::uint64_t next_pc = context.program_counter + 4U;

    if (instruction == 0xD503201FU) { // NOP
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // MRS (read system register): only the explicitly modelled register bank
    // is accepted. Other architectural system registers remain unsupported.
    if ((instruction & 0xFFE00000U) == 0xD5200000U) {
        const auto encoding = static_cast<std::uint16_t>((instruction >> 5U) & 0xFFFFU);
        arch::SystemRegister system_register{};
        if (!arch::decode_system_register(encoding, system_register)) {
            return StepResult::UnsupportedInstruction;
        }
        const unsigned destination = instruction & 0x1FU;
        write_register(context, destination,
                       context.system_registers.read(system_register), true);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // MSR (write system register): read-only and unmodelled registers fail
    // explicitly rather than silently changing architectural state.
    if ((instruction & 0xFFE00000U) == 0xD5000000U) {
        const auto encoding = static_cast<std::uint16_t>((instruction >> 5U) & 0xFFFFU);
        arch::SystemRegister system_register{};
        if (!arch::decode_system_register(encoding, system_register)) {
            return StepResult::UnsupportedInstruction;
        }
        const unsigned source = instruction & 0x1FU;
        const auto access = context.system_registers.write(
            system_register, read_register(context, source));
        if (access == arch::RegisterAccess::ReadOnly) {
            return StepResult::SystemRegisterFault;
        }
        if (access != arch::RegisterAccess::Ok) {
            return StepResult::UnsupportedInstruction;
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // MOVZ: move wide with zero, 32-bit and 64-bit forms.
    if ((instruction & 0x7F800000U) == 0x52800000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned halfword = (instruction >> 21U) & 0x3U;
        if (!is_64_bit && halfword > 1U) {
            return StepResult::UnsupportedInstruction;
        }
        const unsigned shift = halfword * 16U;
        const std::uint64_t immediate = (instruction >> 5U) & 0xFFFFU;
        write_register(context, destination, immediate << shift, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // MOVK: move wide with keep, 32-bit and 64-bit forms.
    if ((instruction & 0x7F800000U) == 0x72800000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned halfword = (instruction >> 21U) & 0x3U;
        if (!is_64_bit && halfword > 1U) {
            return StepResult::UnsupportedInstruction;
        }
        const unsigned shift = halfword * 16U;
        const std::uint64_t field_mask = 0xFFFFULL << shift;
        const std::uint64_t immediate = ((instruction >> 5U) & 0xFFFFU) << shift;
        const std::uint64_t previous = read_register(context, destination);
        write_register(context, destination,
                       (previous & ~field_mask) | immediate, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Unsigned-immediate scalar LDR/STR for byte, halfword, word and X-register sizes.
    // Register 31 is SP for the address base and XZR/WZR for transferred data.
    if ((instruction & 0x3B000000U) == 0x39000000U) {
        const unsigned size = (instruction >> 30U) & 0x3U;
        const unsigned opc = (instruction >> 22U) & 0x3U;
        if (opc > 1U) {
            return StepResult::UnsupportedInstruction;
        }

        const std::size_t transfer_size = std::size_t{1} << size;
        const unsigned target = instruction & 0x1FU;
        const unsigned base_register = (instruction >> 5U) & 0x1FU;
        const std::uint64_t base = read_base_register(context, base_register);
        const std::uint64_t offset =
            static_cast<std::uint64_t>((instruction >> 10U) & 0xFFFU) << size;
        if (base > std::numeric_limits<std::uint64_t>::max() - offset) {
            return StepResult::MemoryFault;
        }
        const std::uint64_t address = base + offset;
        std::array<std::uint8_t, 8> bytes{};

        if (opc == 0U) { // STR
            const std::uint64_t value = read_register(context, target);
            for (std::size_t index = 0; index < transfer_size; ++index) {
                bytes[index] = static_cast<std::uint8_t>(
                    (value >> (index * 8U)) & 0xFFU);
            }
            const auto data = std::span<const std::uint8_t>(
                bytes.data(), transfer_size);
            if (!memory.write(address, data)) {
                return StepResult::MemoryFault;
            }
        } else { // LDR (zero-extends byte/halfword/word loads into W registers)
            const auto data = std::span<std::uint8_t>(
                bytes.data(), transfer_size);
            if (!memory.read(address, data)) {
                return StepResult::MemoryFault;
            }
            std::uint64_t value = 0U;
            for (std::size_t index = 0; index < transfer_size; ++index) {
                value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
            }
            write_register(context, target, value, size == 3U);
        }

        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // ADD/SUB (shifted register), including flag-setting variants.
    if ((instruction & 0x1F200000U) == 0x0B000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const bool subtract = (instruction & 0x40000000U) != 0U;
        const bool set_flags = (instruction & 0x20000000U) != 0U;
        const unsigned shift_kind = (instruction >> 22U) & 0x3U;
        const unsigned amount = (instruction >> 10U) & 0x3FU;
        if (shift_kind == 3U || (!is_64_bit && amount >= 32U))
            return StepResult::UnsupportedInstruction;
        const unsigned destination = instruction & 0x1FU;
        const unsigned left_index = (instruction >> 5U) & 0x1FU;
        const unsigned right_index = (instruction >> 16U) & 0x1FU;
        const std::uint64_t mask = width_mask(is_64_bit);
        const std::uint64_t left = read_register(context, left_index) & mask;
        std::uint64_t right = 0U;
        if (!shifted_operand(read_register(context, right_index), shift_kind,
                             amount, is_64_bit, right))
            return StepResult::UnsupportedInstruction;
        const std::uint64_t result = subtract ? (left - right) & mask
                                              : (left + right) & mask;
        write_register(context, destination, result, is_64_bit);
        if (set_flags) {
            if (subtract) set_sub_flags(context, left, right, result, is_64_bit);
            else set_add_flags(context, left, right, result, is_64_bit);
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // AND/ORR/EOR/ANDS (shifted register), including MOV register alias.
    if ((instruction & 0x1F000000U) == 0x0A000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const unsigned operation = (instruction >> 29U) & 0x3U;
        const bool invert = (instruction & 0x00200000U) != 0U;
        const unsigned shift_kind = (instruction >> 22U) & 0x3U;
        const unsigned amount = (instruction >> 10U) & 0x3FU;
        if (!is_64_bit && amount >= 32U)
            return StepResult::UnsupportedInstruction;
        const unsigned destination = instruction & 0x1FU;
        const unsigned left_index = (instruction >> 5U) & 0x1FU;
        const unsigned right_index = (instruction >> 16U) & 0x1FU;
        const std::uint64_t mask = width_mask(is_64_bit);
        const std::uint64_t left = read_register(context, left_index) & mask;
        std::uint64_t right = 0U;
        if (!shifted_operand(read_register(context, right_index), shift_kind,
                             amount, is_64_bit, right))
            return StepResult::UnsupportedInstruction;
        if (invert) right = (~right) & mask;
        std::uint64_t result = 0U;
        switch (operation) {
        case 0U: result = left & right; break;
        case 1U: result = left | right; break;
        case 2U: result = left ^ right; break;
        case 3U: result = left & right; break;
        }
        result &= mask;
        write_register(context, destination, result, is_64_bit);
        if (operation == 3U) set_logic_flags(context, result, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // ADD/SUB (immediate), with optional NZCV updates (ADDS/SUBS).
    if ((instruction & 0x1F000000U) == 0x11000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const bool subtract = (instruction & 0x40000000U) != 0U;
        const bool set_flags = (instruction & 0x20000000U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned source = (instruction >> 5U) & 0x1FU;
        const unsigned shift = ((instruction >> 22U) & 1U) != 0U ? 12U : 0U;
        const std::uint64_t immediate =
            static_cast<std::uint64_t>((instruction >> 10U) & 0xFFFU) << shift;
        const std::uint64_t mask = width_mask(is_64_bit);
        const std::uint64_t left =
            read_addsub_register(context, source, is_64_bit, set_flags);
        const std::uint64_t right = immediate & mask;
        const std::uint64_t result = subtract ? (left - right) & mask
                                              : (left + right) & mask;
        write_addsub_register(context, destination, result, is_64_bit, set_flags);
        if (set_flags) {
            if (subtract) {
                set_sub_flags(context, left, right, result, is_64_bit);
            } else {
                set_add_flags(context, left, right, result, is_64_bit);
            }
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // B.cond: conditional PC-relative branch using the current NZCV flags.
    if ((instruction & 0xFF000010U) == 0x54000000U) {
        const unsigned condition = instruction & 0xFU;
        if (condition == 0xFU) {
            return StepResult::UnsupportedInstruction;
        }
        if (condition_holds(context.flags, condition)) {
            const auto displacement =
                sign_extend((instruction >> 5U) & 0x7FFFFU, 19U) * 4;
            context.program_counter = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(context.program_counter) + displacement);
            ++context.instructions_retired;
            return StepResult::Branch;
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // B: unconditional PC-relative branch.
    if ((instruction & 0x7C000000U) == 0x14000000U) {
        const auto displacement = sign_extend(instruction & 0x03FFFFFFU, 26U) * 4;
        context.program_counter = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(context.program_counter) + displacement);
        ++context.instructions_retired;
        return StepResult::Branch;
    }

    // CBZ / CBNZ: compare register with zero and branch.
    if ((instruction & 0x7E000000U) == 0x34000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const bool nonzero = (instruction & 0x01000000U) != 0U;
        const unsigned source = instruction & 0x1FU;
        const std::uint64_t value = read_register(context, source);
        const bool is_zero = is_64_bit ? value == 0U
                                       : (value & 0xFFFFFFFFULL) == 0U;
        const bool take_branch = nonzero ? !is_zero : is_zero;
        if (take_branch) {
            const auto displacement =
                sign_extend((instruction >> 5U) & 0x7FFFFU, 19U) * 4;
            context.program_counter = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(context.program_counter) + displacement);
            ++context.instructions_retired;
            return StepResult::Branch;
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // BR / RET: indirect branches. RET defaults to X30.
    const std::uint32_t branch_class = instruction & 0xFFFFFC1FU;
    if (branch_class == 0xD61F0000U || branch_class == 0xD65F0000U) {
        const unsigned source = (instruction >> 5U) & 0x1FU;
        context.program_counter = read_register(context, source);
        ++context.instructions_retired;
        return StepResult::Branch;
    }

    return StepResult::UnsupportedInstruction;
}

} // namespace lucid::cpu
