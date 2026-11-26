#include "lucid/cpu/cpu_backend.hpp"

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

[[nodiscard]] bool fetch_instruction(core::MemoryBus& memory,
                                     const std::uint64_t pc,
                                     std::uint32_t& instruction) {
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(pc, bytes)) {
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
        const std::uint64_t left = read_register(context, source) & mask;
        const std::uint64_t right = immediate & mask;
        const std::uint64_t result = subtract ? (left - right) & mask
                                              : (left + right) & mask;
        write_register(context, destination, result, is_64_bit);
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
