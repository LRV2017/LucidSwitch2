#include "lucid/cpu/cpu_backend.hpp"

#include <array>
#include <cstdint>

namespace lucid::cpu {
namespace {

[[nodiscard]] std::int64_t sign_extend(const std::uint64_t value,
                                       const unsigned bits) noexcept {
    const std::uint64_t sign = std::uint64_t{1} << (bits - 1U);
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1U;
    const std::uint64_t narrowed = value & mask;
    return static_cast<std::int64_t>((narrowed ^ sign) - sign);
}

[[nodiscard]] std::uint64_t read_register(const CpuContext& context,
                                          const unsigned index) noexcept {
    return index == 31U ? 0U : context.x[index];
}

void write_register(CpuContext& context, const unsigned index,
                    const std::uint64_t value, const bool is_64_bit) noexcept {
    if (index == 31U) {
        return; // XZR/WZR for the instructions supported by this subset.
    }
    context.x[index] = is_64_bit ? value : (value & 0xFFFFFFFFULL);
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

    // ADD/SUB (immediate), without flag updates.
    const std::uint32_t arithmetic_class = instruction & 0x1F000000U;
    if (arithmetic_class == 0x11000000U ||
        arithmetic_class == 0x51000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const bool subtract = (instruction & 0x40000000U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned source = (instruction >> 5U) & 0x1FU;
        const unsigned shift = ((instruction >> 22U) & 1U) != 0U ? 12U : 0U;
        const std::uint64_t immediate =
            static_cast<std::uint64_t>((instruction >> 10U) & 0xFFFU) << shift;
        const std::uint64_t left = read_register(context, source);
        const std::uint64_t result = subtract ? left - immediate : left + immediate;
        write_register(context, destination, result, is_64_bit);
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
