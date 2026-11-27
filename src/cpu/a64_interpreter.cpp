#include "lucid/cpu/cpu_backend.hpp"
#include "lucid/arch/system_registers.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace lucid::cpu {
namespace {

[[nodiscard]] std::int64_t sign_extend(const std::uint64_t value,
                                       const unsigned bits) noexcept {
    if (bits == 64U) return std::bit_cast<std::int64_t>(value);
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


[[nodiscard]] bool decode_logical_immediate(
    const bool n, const unsigned immr, const unsigned imms,
    const bool is_64_bit, std::uint64_t& result) noexcept {
    const unsigned width = is_64_bit ? 64U : 32U;
    if (!is_64_bit && n) return false;
    const std::uint32_t selector =
        (static_cast<std::uint32_t>(n) << 6U) | ((~imms) & 0x3FU);
    if (selector == 0U) return false;
    const unsigned length = 31U - static_cast<unsigned>(std::countl_zero(selector));
    if (length < 1U) return false;
    const unsigned levels = (1U << length) - 1U;
    const unsigned s_value = imms & levels;
    const unsigned r_value = immr & levels;
    if (s_value == levels) return false;
    const unsigned element_size = 1U << length;
    if (element_size > width) return false;
    const unsigned ones = s_value + 1U;
    std::uint64_t element = ones == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : ((std::uint64_t{1} << ones) - 1U);
    const std::uint64_t element_mask = element_size == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : ((std::uint64_t{1} << element_size) - 1U);
    const unsigned rotation = r_value % element_size;
    if (rotation != 0U)
        element = ((element >> rotation) |
                   (element << (element_size - rotation))) & element_mask;
    result = 0U;
    for (unsigned bit = 0U; bit < width; bit += element_size)
        result |= element << bit;
    result &= width_mask(is_64_bit);
    return true;
}
[[nodiscard]] std::uint64_t rotate_right(
    const std::uint64_t value, const unsigned amount,
    const unsigned width) noexcept {
    const std::uint64_t mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max() : 0xFFFFFFFFULL;
    const unsigned shift = amount % width;
    if (shift == 0U) return value & mask;
    return ((value >> shift) | (value << (width - shift))) & mask;
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
    if ((context.program_counter & 3U) != 0U)
        return StepResult::MisalignedProgramCounter;
    std::uint32_t instruction = 0U;
    if (!fetch_instruction(memory, context.program_counter, instruction))
        return StepResult::MemoryFault;
    return step_decoded(context, memory, instruction);
}

StepResult A64Interpreter::step_decoded(CpuContext& context, CpuMemory& memory,
                                        const std::uint32_t instruction) {
    if ((context.program_counter & 3U) != 0U)
        return StepResult::MisalignedProgramCounter;
    const std::uint64_t next_pc = context.program_counter + 4U;


    // DMB/DSB/ISB: in this interpreter every guest instruction completes
    // synchronously, memory accesses are immediately visible, and instructions
    // are fetched afresh on every step. The architectural ordering/completion
    // points are therefore already satisfied by this single-threaded model.
    // This is not a substitute for barriers in a future concurrent/JIT core.
    const std::uint32_t barrier_encoding = instruction & 0xFFFFF0FFU;
    const unsigned barrier_option = (instruction >> 8U) & 0xFU;
    if ((barrier_encoding == 0xD50330BFU || // DMB <option>
         barrier_encoding == 0xD503309FU) && // DSB <option>
        barrier_option != 0U) {
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }
    if (instruction == 0xD5033FDFU) { // ISB SY
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // LDAR/STLR: aligned word/doubleword acquire loads and release stores.
    // The current one-instruction-at-a-time execution model already sequences
    // each memory operation; no host-thread ordering primitive is implied.
    const std::uint32_t acquire_release_encoding = instruction & 0x3FFFFC00U;
    if (acquire_release_encoding == 0x08DFFC00U ||
        acquire_release_encoding == 0x089FFC00U) {
        const unsigned size = (instruction >> 30U) & 0x3U;
        if (size < 2U) return StepResult::UnsupportedInstruction;
        const std::size_t transfer_size = std::size_t{1U} << size;
        const bool load = acquire_release_encoding == 0x08DFFC00U;
        const unsigned target = instruction & 0x1FU;
        const unsigned base_register = (instruction >> 5U) & 0x1FU;
        const std::uint64_t address = read_base_register(context, base_register);
        if ((address % transfer_size) != 0U) return StepResult::MemoryFault;
        std::array<std::uint8_t, 8U> bytes{};
        if (load) {
            if (!memory.read(address, std::span<std::uint8_t>(
                    bytes.data(), transfer_size)))
                return StepResult::MemoryFault;
            std::uint64_t value = 0U;
            for (std::size_t byte = 0U; byte < transfer_size; ++byte)
                value |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
            write_register(context, target, value, size == 3U);
        } else {
            const std::uint64_t value =
                read_register(context, target) & width_mask(size == 3U);
            for (std::size_t byte = 0U; byte < transfer_size; ++byte)
                bytes[byte] = static_cast<std::uint8_t>(
                    (value >> (byte * 8U)) & 0xFFU);
            if (!memory.write(address, std::span<const std::uint8_t>(
                    bytes.data(), transfer_size)))
                return StepResult::MemoryFault;
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // CLREX: discard this core's local exclusive reservation.
    if (instruction == 0xD5033F5FU) {
        context.clear_exclusive_monitor();
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // LDXR / LDAXR (W/X): acquire exclusive reservation on the translated
    // physical address. Acquire ordering is sequential in this single-host-
    // thread execution model; no true host-thread memory ordering is claimed.
    if ((instruction & 0x3FFF7C00U) == 0x085F7C00U) {
        const unsigned size = (instruction >> 30U) & 0x3U;
        if (size < 2U) return StepResult::UnsupportedInstruction;
        const std::size_t transfer_size = std::size_t{1} << size;
        const unsigned target = instruction & 0x1FU;
        const unsigned base_register = (instruction >> 5U) & 0x1FU;
        const std::uint64_t address = read_base_register(context, base_register);
        if ((address % transfer_size) != 0U) return StepResult::MemoryFault;

        std::array<std::uint8_t, 8U> bytes{};
        std::uint64_t physical_address = 0U;
        std::uint64_t version = 0U;
        if (!memory.read_exclusive(address,
                std::span<std::uint8_t>(bytes.data(), transfer_size),
                physical_address, version)) {
            return StepResult::MemoryFault;
        }

        std::uint64_t value = 0U;
        for (std::size_t byte = 0U; byte < transfer_size; ++byte)
            value |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
        write_register(context, target, value, size == 3U);
        context.exclusive_monitor_valid = true;
        context.exclusive_address = physical_address;
        context.exclusive_size = static_cast<std::uint32_t>(transfer_size);
        context.exclusive_version = version;
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // STXR / STLXR (W/X): the local reservation must match both translated
    // address and transfer size, and the shared reservation version must be
    // unchanged. Any completed attempt consumes the local reservation.
    if ((instruction & 0x3FE07C00U) == 0x08007C00U) {
        const unsigned size = (instruction >> 30U) & 0x3U;
        if (size < 2U) return StepResult::UnsupportedInstruction;
        const std::size_t transfer_size = std::size_t{1} << size;
        const unsigned status_register = (instruction >> 16U) & 0x1FU;
        const unsigned target = instruction & 0x1FU;
        const unsigned base_register = (instruction >> 5U) & 0x1FU;
        const std::uint64_t address = read_base_register(context, base_register);
        if ((address % transfer_size) != 0U) return StepResult::MemoryFault;

        bool stored = false;
        if (context.exclusive_monitor_valid &&
            context.exclusive_size == transfer_size) {
            std::array<std::uint8_t, 8U> bytes{};
            const std::uint64_t value = read_register(context, target);
            for (std::size_t byte = 0U; byte < transfer_size; ++byte)
                bytes[byte] = static_cast<std::uint8_t>(
                    (value >> (byte * 8U)) & 0xFFU);
            if (!memory.write_exclusive(address,
                    std::span<const std::uint8_t>(bytes.data(), transfer_size),
                    context.exclusive_address, context.exclusive_version, stored)) {
                return StepResult::MemoryFault;
            }
        }
        context.clear_exclusive_monitor();
        write_register(context, status_register, stored ? 0U : 1U, false);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }


    // LSE compare-and-swap pair (CASP/CASPA/CASPL/CASPAL). The
    // even-numbered register pairs contain expected and replacement values.
    if ((instruction & 0x3FA07C00U) == 0x08207C00U) {
        const unsigned size_encoding=(instruction>>30U)&1U;
        const std::size_t element_size=size_encoding!=0U?8U:4U;
        const std::size_t total_size=element_size*2U;
        const unsigned expected_register=(instruction>>16U)&0x1FU;
        const unsigned base_register=(instruction>>5U)&0x1FU;
        const unsigned desired_register=instruction&0x1FU;
        const bool acquire=((instruction>>22U)&1U)!=0U;
        const bool release=((instruction>>15U)&1U)!=0U;
        (void)acquire;
        (void)release;
        if((expected_register&1U)!=0U||(desired_register&1U)!=0U)
            return StepResult::UnsupportedInstruction;
        const std::uint64_t address=read_base_register(context,base_register);
        if((address%total_size)!=0U)return StepResult::MemoryFault;
        const std::uint64_t expected_low=read_register(context,expected_register);
        const std::uint64_t expected_high=read_register(context,expected_register+1U);
        const std::uint64_t desired_low=read_register(context,desired_register);
        const std::uint64_t desired_high=read_register(context,desired_register+1U);
        std::uint64_t previous_low=0U,previous_high=0U;
        bool exchanged=false;
        if(!memory.atomic_compare_exchange_pair(address,element_size,
            expected_low,expected_high,desired_low,desired_high,
            previous_low,previous_high,exchanged))return StepResult::MemoryFault;
        (void)exchanged;
        write_register(context,expected_register,previous_low,size_encoding==1U);
        write_register(context,expected_register+1U,previous_high,size_encoding==1U);
        context.program_counter=next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // AArch64 FEAT_LSE compare-and-swap. Rs contains the expected value
    // and is replaced with the old memory value; Rt supplies the new value.
    if ((instruction & 0x3FA07C00U) == 0x08A07C00U) {
        const unsigned size_encoding=(instruction>>30U)&0x3U;
        const std::size_t transfer_size=std::size_t{1U}<<size_encoding;
        const unsigned expected_register=(instruction>>16U)&0x1FU;
        const unsigned base_register=(instruction>>5U)&0x1FU;
        const unsigned desired_register=instruction&0x1FU;
        const std::uint64_t address=read_base_register(context,base_register);
        if((address%transfer_size)!=0U)return StepResult::MemoryFault;
        const std::uint64_t expected=read_register(context,expected_register);
        const std::uint64_t desired=read_register(context,desired_register);
        std::uint64_t previous=0U;
        bool exchanged=false;
        if(!memory.atomic_compare_exchange(address,transfer_size,expected,
                                           desired,previous,exchanged))
            return StepResult::MemoryFault;
        (void)exchanged;
        write_register(context,expected_register,previous,size_encoding==3U);
        context.program_counter=next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // AArch64 FEAT_LSE atomic memory operations. One MemoryBus lock spans
    // the complete read/modify/write and updates exclusive-monitor versions.
    if ((instruction & 0x3F200C00U) == 0x38200000U) {
        const unsigned size_encoding=(instruction>>30U)&0x3U;
        const std::size_t transfer_size=std::size_t{1U}<<size_encoding;
        const bool acquire=((instruction>>23U)&1U)!=0U;
        const bool release=((instruction>>22U)&1U)!=0U;
        const unsigned source_register=(instruction>>16U)&0x1FU;
        const unsigned operation_field=(instruction>>12U)&0x7U;
        const bool swap=((instruction>>15U)&1U)!=0U;
        const unsigned base_register=(instruction>>5U)&0x1FU;
        const unsigned target_register=instruction&0x1FU;
        (void)acquire;
        (void)release;
        core::AtomicOperation operation{};
        if(swap){
            if(operation_field!=0U)return StepResult::UnsupportedInstruction;
            operation=core::AtomicOperation::Swap;
        }else{
            switch(operation_field){
            case 0U:operation=core::AtomicOperation::Add;break;
            case 1U:operation=core::AtomicOperation::Clear;break;
            case 2U:operation=core::AtomicOperation::Eor;break;
            case 3U:operation=core::AtomicOperation::Set;break;
            case 4U:operation=core::AtomicOperation::SignedMax;break;
            case 5U:operation=core::AtomicOperation::SignedMin;break;
            case 6U:operation=core::AtomicOperation::UnsignedMax;break;
            case 7U:operation=core::AtomicOperation::UnsignedMin;break;
            }
        }
        const std::uint64_t address=read_base_register(context,base_register);
        if((address%transfer_size)!=0U)return StepResult::MemoryFault;
        const std::uint64_t operand=read_register(context,source_register);
        std::uint64_t previous=0U;
        if(!memory.atomic_rmw(address,transfer_size,operand,operation,previous))
            return StepResult::MemoryFault;
        write_register(context,target_register,previous,size_encoding==3U);
        context.program_counter=next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Initial Advanced SIMD integer operations. This bounded subset supports
    // vector ADD/SUB for B/H/S/D lanes and vector AND/ORR/EOR at 64/128 bits.
    if ((instruction & 0xBF20FC00U) == 0x0E208400U ||
        (instruction & 0xBF20FC00U) == 0x2E208400U) {
        const bool q = ((instruction >> 30U) & 1U) != 0U;
        const bool subtract = (instruction & 0x20000000U) != 0U;
        const unsigned size = (instruction >> 22U) & 0x3U;
        if (size == 3U && !q) return StepResult::UnsupportedInstruction;

        const unsigned destination = instruction & 0x1FU;
        const unsigned left_index = (instruction >> 5U) & 0x1FU;
        const unsigned right_index = (instruction >> 16U) & 0x1FU;
        const unsigned element_bits = 8U << size;
        const unsigned lanes_per_half = 64U / element_bits;
        const unsigned datasize_bits = q ? 128U : 64U;
        const unsigned element_count = datasize_bits / element_bits;
        const std::uint64_t element_mask = element_bits == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : ((std::uint64_t{1} << element_bits) - 1U);
        const SimdRegister left = context.v[left_index];
        const SimdRegister right = context.v[right_index];
        SimdRegister result{};

        for (unsigned lane = 0U; lane < element_count; ++lane) {
            const unsigned half = lane / lanes_per_half;
            const unsigned shift = (lane % lanes_per_half) * element_bits;
            const std::uint64_t left_word = half == 0U ? left.low : left.high;
            const std::uint64_t right_word = half == 0U ? right.low : right.high;
            const std::uint64_t a = (left_word >> shift) & element_mask;
            const std::uint64_t b = (right_word >> shift) & element_mask;
            const std::uint64_t value =
                (subtract ? (a - b) : (a + b)) & element_mask;
            std::uint64_t& result_word = half == 0U ? result.low : result.high;
            result_word |= value << shift;
        }

        // A 64-bit SIMD destination write clears the upper 64 bits.
        if (!q) result.high = 0U;
        context.v[destination] = result;
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Bitwise vector AND, ORR and EOR. These operate on the selected 64-bit
    // or 128-bit register width and do not alter the integer NZCV flags.
    const std::uint32_t simd_logic_opcode = instruction & 0xBFE0FC00U;
    if (simd_logic_opcode == 0x0E201C00U ||
        simd_logic_opcode == 0x0EA01C00U ||
        simd_logic_opcode == 0x2E201C00U) {
        const bool q = ((instruction >> 30U) & 1U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned left_index = (instruction >> 5U) & 0x1FU;
        const unsigned right_index = (instruction >> 16U) & 0x1FU;
        const SimdRegister left = context.v[left_index];
        const SimdRegister right = context.v[right_index];
        SimdRegister result{};
        if (simd_logic_opcode == 0x0E201C00U) {
            result.low = left.low & right.low;
            if (q) result.high = left.high & right.high;
        } else if (simd_logic_opcode == 0x0EA01C00U) {
            result.low = left.low | right.low;
            if (q) result.high = left.high | right.high;
        } else {
            result.low = left.low ^ right.low;
            if (q) result.high = left.high ^ right.high;
        }
        context.v[destination] = result;
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // SVC #imm16: take a synchronous exception to the EL1 vector table.
    if ((instruction & 0xFFE0001FU) == 0xD4000001U) {
        const auto source_el = context.system_registers.current_el();
        const auto immediate = static_cast<std::uint64_t>(
            (instruction >> 5U) & 0xFFFFU);
        constexpr std::uint64_t svc_exception_class = 0x15ULL << 26U;
        const arch::ExceptionRecord exception{
            arch::ExceptionKind::Synchronous,
            source_el,
            next_pc,
            svc_exception_class | immediate,
            0U,
            context.pstate()
        };
        const arch::ExceptionRouter router;
        context.program_counter =
            router.enter(context.system_registers, exception);
        ++context.instructions_retired;
        return StepResult::ExceptionTaken;
    }

    // ERET: restore the saved EL, DAIF, SPSel, and NZCV state.
    if (instruction == 0xD69F03E0U) {
        if (context.system_registers.current_el() != 1U) {
            return StepResult::SystemRegisterFault;
        }
        const auto saved_pstate = context.system_registers.read(
            arch::SystemRegister::SPSR_EL1);
        const arch::ExceptionRouter router;
        auto return_pc = context.program_counter;
        if (!router.return_from_exception(context.system_registers, return_pc)) {
            return StepResult::SystemRegisterFault;
        }
        context.restore_nzcv(saved_pstate);
        context.program_counter = return_pc;
        ++context.instructions_retired;
        return StepResult::Branch;
    }

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
        if (context.system_registers.current_el() == 0U &&
            system_register != arch::SystemRegister::CurrentEL) {
            return StepResult::SystemRegisterFault;
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
        if (context.system_registers.current_el() == 0U &&
            system_register != arch::SystemRegister::CurrentEL) {
            return StepResult::SystemRegisterFault;
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


    // MOVN: move wide with NOT, for both W and X register forms.
    if ((instruction & 0x7F800000U) == 0x12800000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned halfword = (instruction >> 21U) & 0x3U;
        if (!is_64_bit && halfword > 1U) return StepResult::UnsupportedInstruction;
        const unsigned shift = halfword * 16U;
        const std::uint64_t immediate =
            static_cast<std::uint64_t>((instruction >> 5U) & 0xFFFFU) << shift;
        write_register(context, destination, ~immediate, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // ADR / ADRP: signed PC-relative address generation.
    if ((instruction & 0x1F000000U) == 0x10000000U) {
        const bool page_relative = (instruction & 0x80000000U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const std::uint64_t immediate_bits =
            (((instruction >> 5U) & 0x7FFFFU) << 2U) |
            ((instruction >> 29U) & 0x3U);
        const std::int64_t immediate = sign_extend(immediate_bits, 21U);
        const std::uint64_t pc_base = page_relative
            ? (context.program_counter & ~0xFFFULL) : context.program_counter;
        const std::int64_t scaled = page_relative ? immediate * 4096LL : immediate;
        const std::uint64_t address = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(pc_base) + scaled);
        write_register(context, destination, address, true);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Logical immediate: AND, ORR, EOR and ANDS.
    if ((instruction & 0x1F800000U) == 0x12000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const unsigned operation = (instruction >> 29U) & 0x3U;
        const bool n = ((instruction >> 22U) & 1U) != 0U;
        const unsigned immr = (instruction >> 16U) & 0x3FU;
        const unsigned imms = (instruction >> 10U) & 0x3FU;
        std::uint64_t immediate = 0U;
        if (!decode_logical_immediate(n, immr, imms, is_64_bit, immediate))
            return StepResult::UnsupportedInstruction;
        const unsigned destination = instruction & 0x1FU;
        const unsigned source = (instruction >> 5U) & 0x1FU;
        const std::uint64_t left =
            read_register(context, source) & width_mask(is_64_bit);
        std::uint64_t result = 0U;
        switch (operation) {
        case 0U: result = left & immediate; break;
        case 1U: result = left | immediate; break;
        case 2U: result = left ^ immediate; break;
        case 3U: result = left & immediate; break;
        }
        result &= width_mask(is_64_bit);
        write_register(context, destination, result, is_64_bit);
        if (operation == 3U) set_logic_flags(context, result, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }


    // ADC/ADCS and SBC/SBCS with carry-in / borrow-in semantics.
    if ((instruction & 0x1FE0FC00U) == 0x1A000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const bool subtract = (instruction & 0x40000000U) != 0U;
        const bool set_flags = (instruction & 0x20000000U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned left_index = (instruction >> 5U) & 0x1FU;
        const unsigned right_index = (instruction >> 16U) & 0x1FU;
        const std::uint64_t mask = width_mask(is_64_bit);
        const unsigned sign_bit = is_64_bit ? 63U : 31U;
        const std::uint64_t left = read_register(context, left_index) & mask;
        const std::uint64_t right = read_register(context, right_index) & mask;
        std::uint64_t result = 0U;
        bool carry_out = false;
        if (!subtract) {
            const std::uint64_t carry_in = context.flags.carry ? 1U : 0U;
            if (is_64_bit) {
                const std::uint64_t first = left + right;
                const bool first_carry = first < left;
                result = first + carry_in;
                const bool second_carry = result < first;
                carry_out = first_carry || second_carry;
            } else {
                const std::uint64_t full = left + right + carry_in;
                result = full & mask;
                carry_out = full > mask;
            }
        } else {
            const std::uint64_t borrow_in = context.flags.carry ? 0U : 1U;
            result = (left - right - borrow_in) & mask;
            carry_out = borrow_in == 0U ? left >= right : left > right;
        }

        write_register(context, destination, result, is_64_bit);
        if (set_flags) {
            context.flags.negative =
                ((result >> sign_bit) & 1U) != 0U;
            context.flags.zero = result == 0U;
            context.flags.carry = carry_out;
            if (!subtract) {
                context.flags.overflow =
                    (((~(left ^ right) & (left ^ result)) >> sign_bit) & 1U) != 0U;
            } else {
                context.flags.overflow =
                    ((((left ^ right) & (left ^ result)) >> sign_bit) & 1U) != 0U;
            }
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Conditional select family: CSEL, CSINC, CSINV and CSNEG.
    if ((instruction & 0x1FE00000U) == 0x1A800000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const bool invert = (instruction & 0x40000000U) != 0U;
        const bool increment = (instruction & 0x00000400U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned left_index = (instruction >> 5U) & 0x1FU;
        const unsigned condition = (instruction >> 12U) & 0xFU;
        const unsigned right_index = (instruction >> 16U) & 0x1FU;
        const bool take_left = condition_holds(context.flags, condition);
        std::uint64_t result = take_left
            ? read_register(context, left_index) : read_register(context, right_index);
        if (!take_left) {
            if (invert) result = ~result;
            if (increment) result += 1U;
        }
        write_register(context, destination, result, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Data-processing two-source: UDIV/SDIV and variable shifts.
    const std::uint32_t two_source_opcode = instruction & 0x7FE0FC00U;
    if (two_source_opcode == 0x1AC00800U || two_source_opcode == 0x1AC00C00U ||
        two_source_opcode == 0x1AC02000U || two_source_opcode == 0x1AC02400U ||
        two_source_opcode == 0x1AC02800U || two_source_opcode == 0x1AC02C00U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned left_index = (instruction >> 5U) & 0x1FU;
        const unsigned right_index = (instruction >> 16U) & 0x1FU;
        const std::uint64_t mask = width_mask(is_64_bit);
        const std::uint64_t left = read_register(context, left_index) & mask;
        const std::uint64_t right = read_register(context, right_index) & mask;
        const unsigned width = is_64_bit ? 64U : 32U;
        const unsigned shift = static_cast<unsigned>(right & (width - 1U));
        std::uint64_t result = 0U;
        switch (two_source_opcode) {
        case 0x1AC00800U:
            result = right == 0U ? 0U : left / right;
            break;
        case 0x1AC00C00U:
            if (is_64_bit) {
                const std::int64_t a = std::bit_cast<std::int64_t>(left);
                const std::int64_t b = std::bit_cast<std::int64_t>(right);
                if (b == 0) result = 0U;
                else if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
                    result = static_cast<std::uint64_t>(a);
                else result = static_cast<std::uint64_t>(a / b);
            } else {
                const std::int32_t a = std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(left));
                const std::int32_t b = std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(right));
                if (b == 0) result = 0U;
                else if (a == std::numeric_limits<std::int32_t>::min() && b == -1)
                    result = static_cast<std::uint32_t>(a);
                else result = static_cast<std::uint32_t>(a / b);
            }
            break;
        case 0x1AC02000U: result = (left << shift) & mask; break;
        case 0x1AC02400U: result = left >> shift; break;
        case 0x1AC02800U:
            if (is_64_bit)
                result = static_cast<std::uint64_t>(
                    std::bit_cast<std::int64_t>(left) >> shift) & mask;
            else
                result = static_cast<std::uint32_t>(
                    std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(left)) >> shift);
            break;
        case 0x1AC02C00U: result = rotate_right(left, shift, width); break;
        default: return StepResult::UnsupportedInstruction;
        }
        write_register(context, destination, result, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // MADD/MSUB and MUL alias (32-bit and 64-bit).
    if ((instruction & 0x1F000000U) == 0x1B000000U) {
        const bool is_64_bit = (instruction >> 31U) != 0U;
        const bool subtract = (instruction & 0x00008000U) != 0U;
        const unsigned destination = instruction & 0x1FU;
        const unsigned multiplicand_a = (instruction >> 5U) & 0x1FU;
        const unsigned accumulator = (instruction >> 10U) & 0x1FU;
        const unsigned multiplicand_b = (instruction >> 16U) & 0x1FU;
        const std::uint64_t mask = width_mask(is_64_bit);
        const std::uint64_t a = read_register(context, multiplicand_a) & mask;
        const std::uint64_t b = read_register(context, multiplicand_b) & mask;
        const std::uint64_t addend = read_register(context, accumulator) & mask;
        const std::uint64_t product = (a * b) & mask;
        const std::uint64_t result = subtract
            ? (addend - product) & mask : (product + addend) & mask;
        write_register(context, destination, result, is_64_bit);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Scalar floating-point moves and arithmetic. This foundation models
    // only the S and D scalar lanes; vector/NEON operations and FPCR/FPSR
    // architectural control are intentionally not implied.
    const std::uint32_t fp_opcode = instruction & 0xFFE0FC00U;
    const std::uint32_t fp_move_opcode = instruction & 0xFFFFFC00U;
    const unsigned fp_destination = instruction & 0x1FU;
    const unsigned fp_source_n = (instruction >> 5U) & 0x1FU;
    const unsigned fp_source_m = (instruction >> 16U) & 0x1FU;

    if (fp_move_opcode == 0x1E270000U || fp_move_opcode == 0x9E670000U) {
        // FMOV Sd,Wn / FMOV Dd,Xn (bit-preserving general-to-FP transfer).
        const bool is_double = fp_move_opcode == 0x9E670000U;
        context.v[fp_destination].low = is_double
            ? read_register(context, fp_source_n)
            : (read_register(context, fp_source_n) & 0xFFFFFFFFULL);
        context.v[fp_destination].high = 0U;
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    if (fp_move_opcode == 0x1E260000U || fp_move_opcode == 0x9E660000U) {
        // FMOV Wd,Sn / FMOV Xd,Dn (bit-preserving FP-to-general transfer).
        const bool is_double = fp_move_opcode == 0x9E660000U;
        write_register(context, fp_destination,
                       context.v[fp_source_n].low, is_double);
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    if (fp_move_opcode == 0x1E204000U || fp_move_opcode == 0x1E604000U) {
        // FMOV Sd,Sn / FMOV Dd,Dn (scalar lane copy).
        const bool is_double = fp_move_opcode == 0x1E604000U;
        context.v[fp_destination].low = is_double
            ? context.v[fp_source_n].low
            : (context.v[fp_source_n].low & 0xFFFFFFFFULL);
        context.v[fp_destination].high = 0U;
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    enum class ScalarFpOperation { None, Multiply, Divide, Add, Subtract };
    ScalarFpOperation fp_operation = ScalarFpOperation::None;
    switch (fp_opcode) {
    case 0x1E200800U: case 0x1E600800U:
        fp_operation = ScalarFpOperation::Multiply; break;
    case 0x1E201800U: case 0x1E601800U:
        fp_operation = ScalarFpOperation::Divide; break;
    case 0x1E202800U: case 0x1E602800U:
        fp_operation = ScalarFpOperation::Add; break;
    case 0x1E203800U: case 0x1E603800U:
        fp_operation = ScalarFpOperation::Subtract; break;
    default: break;
    }

    if (fp_operation != ScalarFpOperation::None) {
        const bool is_double = (fp_opcode & 0x00400000U) != 0U;
        if (is_double) {
            const double left = std::bit_cast<double>(
                context.v[fp_source_n].low);
            const double right = std::bit_cast<double>(
                context.v[fp_source_m].low);
            double result = 0.0;
            switch (fp_operation) {
            case ScalarFpOperation::Multiply: result = left * right; break;
            case ScalarFpOperation::Divide: result = left / right; break;
            case ScalarFpOperation::Add: result = left + right; break;
            case ScalarFpOperation::Subtract: result = left - right; break;
            case ScalarFpOperation::None: break;
            }
            context.v[fp_destination].low = std::bit_cast<std::uint64_t>(result);
        } else {
            const float left = std::bit_cast<float>(
                static_cast<std::uint32_t>(context.v[fp_source_n].low));
            const float right = std::bit_cast<float>(
                static_cast<std::uint32_t>(context.v[fp_source_m].low));
            float result = 0.0F;
            switch (fp_operation) {
            case ScalarFpOperation::Multiply: result = left * right; break;
            case ScalarFpOperation::Divide: result = left / right; break;
            case ScalarFpOperation::Add: result = left + right; break;
            case ScalarFpOperation::Subtract: result = left - right; break;
            case ScalarFpOperation::None: break;
            }
            context.v[fp_destination].low =
                static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(result));
        }
        context.v[fp_destination].high = 0U;
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }



    // PC-relative literal loads for W/X registers and LDRSW. PRFM is a
    // non-memory hint in this subset and retires as a no-op.
    if ((instruction & 0x3B000000U) == 0x18000000U) {
        if ((instruction & 0x04000000U) != 0U)
            return StepResult::UnsupportedInstruction;
        const unsigned opc = (instruction >> 30U) & 0x3U;
        const unsigned target = instruction & 0x1FU;
        const std::int64_t displacement =
            sign_extend((instruction >> 5U) & 0x7FFFFU, 19U) * 4;
        const std::uint64_t pc = context.program_counter;
        std::uint64_t address = pc;
        if (displacement < 0) {
            const std::uint64_t magnitude =
                static_cast<std::uint64_t>(-displacement);
            if (magnitude > pc) return StepResult::MemoryFault;
            address = pc - magnitude;
        } else {
            const std::uint64_t amount =
                static_cast<std::uint64_t>(displacement);
            if (pc > std::numeric_limits<std::uint64_t>::max() - amount)
                return StepResult::MemoryFault;
            address = pc + amount;
        }

        if (opc == 3U) { // PRFM literal: prefetch hints have no data result.
            context.program_counter = next_pc;
            ++context.instructions_retired;
            return StepResult::Executed;
        }

        const std::size_t transfer_size = opc == 1U ? 8U : 4U;
        std::array<std::uint8_t, 8U> bytes{};
        if (!memory.read(address, std::span<std::uint8_t>(
                bytes.data(), transfer_size)))
            return StepResult::MemoryFault;
        std::uint64_t value = 0U;
        for (std::size_t byte = 0U; byte < transfer_size; ++byte)
            value |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
        if (opc == 2U) {
            value = static_cast<std::uint64_t>(
                sign_extend(value, 32U));
            write_register(context, target, value, true);
        } else {
            write_register(context, target, value, opc == 1U);
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // LDP/STP register pairs: integer W/X and SIMD/FP S/D/Q forms.
    // Supports signed-offset, pre-index and post-index addressing.
    if ((instruction & 0x3A000000U) == 0x28000000U) {
        const bool vector_pair = (instruction & 0x04000000U) != 0U;
        const unsigned opc = (instruction >> 30U) & 0x3U;
        const bool load = (instruction & 0x00400000U) != 0U;
        const unsigned mode = (instruction >> 23U) & 0x3U;
        const unsigned target_first = instruction & 0x1FU;
        const unsigned base_register = (instruction >> 5U) & 0x1FU;
        const unsigned target_second = (instruction >> 10U) & 0x1FU;
        const std::int64_t immediate =
            sign_extend((instruction >> 15U) & 0x7FU, 7U);
        if (mode == 0U || opc == 3U ||
            (!vector_pair && opc == 1U && !load)) {
            return StepResult::UnsupportedInstruction;
        }
        if (load && target_first == target_second)
            return StepResult::UnsupportedInstruction;

        const std::size_t element_size = vector_pair
            ? (std::size_t{4U} << opc)
            : (opc == 2U ? 8U : 4U);
        const std::size_t transfer_size = element_size * 2U;
        const std::int64_t byte_displacement =
            immediate * static_cast<std::int64_t>(element_size);
        const std::uint64_t base = read_base_register(context, base_register);
        std::uint64_t effective_address = base;
        std::uint64_t writeback_value = base;
        const bool writeback = mode == 1U || mode == 3U;

        if (mode == 2U || mode == 3U) {
            if (byte_displacement < 0) {
                const std::uint64_t magnitude =
                    static_cast<std::uint64_t>(-byte_displacement);
                if (magnitude > base) return StepResult::MemoryFault;
                effective_address = base - magnitude;
            } else {
                const std::uint64_t amount =
                    static_cast<std::uint64_t>(byte_displacement);
                if (base > std::numeric_limits<std::uint64_t>::max() - amount)
                    return StepResult::MemoryFault;
                effective_address = base + amount;
            }
            if (mode == 3U) writeback_value = effective_address;
        } else {
            if (byte_displacement < 0) {
                const std::uint64_t magnitude =
                    static_cast<std::uint64_t>(-byte_displacement);
                if (magnitude > base) return StepResult::MemoryFault;
                writeback_value = base - magnitude;
            } else {
                const std::uint64_t amount =
                    static_cast<std::uint64_t>(byte_displacement);
                if (base > std::numeric_limits<std::uint64_t>::max() - amount)
                    return StepResult::MemoryFault;
                writeback_value = base + amount;
            }
        }

        if (!vector_pair && writeback && base_register != 31U &&
            (base_register == target_first || base_register == target_second))
            return StepResult::UnsupportedInstruction;

        std::array<std::uint8_t, 32U> bytes{};
        const auto transfer = std::span<std::uint8_t>(
            bytes.data(), transfer_size);
        if (load) {
            if (!memory.read(effective_address, transfer))
                return StepResult::MemoryFault;
            for (unsigned index = 0U; index < 2U; ++index) {
                const std::size_t offset =
                    static_cast<std::size_t>(index) * element_size;
                const unsigned destination =
                    index == 0U ? target_first : target_second;
                std::uint64_t low = 0U;
                for (std::size_t byte = 0U; byte <
                     std::min<std::size_t>(element_size, 8U); ++byte)
                    low |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8U);

                if (vector_pair) {
                    std::uint64_t high = 0U;
                    if (element_size == 16U) {
                        for (std::size_t byte = 0U; byte < 8U; ++byte)
                            high |= static_cast<std::uint64_t>(
                                bytes[offset + 8U + byte]) << (byte * 8U);
                    }
                    context.v[destination] = {low, high};
                } else if (opc == 1U) {
                    const auto signed_word = std::bit_cast<std::int32_t>(
                        static_cast<std::uint32_t>(low));
                    low = static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(signed_word));
                    write_register(context, destination, low, true);
                } else {
                    write_register(context, destination, low, opc == 2U);
                }
            }
        } else {
            for (unsigned index = 0U; index < 2U; ++index) {
                const unsigned source =
                    index == 0U ? target_first : target_second;
                const std::size_t offset =
                    static_cast<std::size_t>(index) * element_size;
                std::uint64_t low = vector_pair
                    ? context.v[source].low
                    : (read_register(context, source) & width_mask(opc == 2U));
                for (std::size_t byte = 0U; byte <
                     std::min<std::size_t>(element_size, 8U); ++byte)
                    bytes[offset + byte] = static_cast<std::uint8_t>(
                        (low >> (byte * 8U)) & 0xFFU);
                if (vector_pair && element_size == 16U) {
                    const std::uint64_t high = context.v[source].high;
                    for (std::size_t byte = 0U; byte < 8U; ++byte)
                        bytes[offset + 8U + byte] = static_cast<std::uint8_t>(
                            (high >> (byte * 8U)) & 0xFFU);
                }
            }
            const auto const_transfer = std::span<const std::uint8_t>(
                bytes.data(), transfer_size);
            if (!memory.write(effective_address, const_transfer))
                return StepResult::MemoryFault;
        }

        if (writeback) {
            if (base_register == 31U) context.sp = writeback_value;
            else context.x[base_register] = writeback_value;
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }


    // Register-offset integer loads/stores with UXTW, LSL, SXTW or SXTX.
    if ((instruction & 0x3B200C00U) == 0x38200800U) {
        if ((instruction & 0x04000000U) != 0U)
            return StepResult::UnsupportedInstruction;
        const unsigned size = (instruction >> 30U) & 0x3U;
        const unsigned opc = (instruction >> 22U) & 0x3U;
        const unsigned target = instruction & 0x1FU;
        const unsigned base_register = (instruction >> 5U) & 0x1FU;
        const unsigned index_register = (instruction >> 16U) & 0x1FU;
        const unsigned option = (instruction >> 13U) & 0x7U;
        const bool scaled = (instruction & 0x1000U) != 0U;
        if ((option != 2U && option != 3U &&
             option != 6U && option != 7U) ||
            (opc == 2U && size == 3U) ||
            (opc == 3U && size > 1U)) {
            return StepResult::UnsupportedInstruction;
        }

        bool negative_index = false;
        std::uint64_t index_magnitude = 0U;
        if (option == 2U) {
            index_magnitude = read_register(context, index_register) & 0xFFFFFFFFULL;
        } else if (option == 3U) {
            index_magnitude = read_register(context, index_register);
        } else {
            std::int64_t signed_index = 0;
            if (option == 6U) {
                signed_index = static_cast<std::int64_t>(
                    std::bit_cast<std::int32_t>(
                        static_cast<std::uint32_t>(
                            read_register(context, index_register))));
            } else {
                signed_index = std::bit_cast<std::int64_t>(
                    read_register(context, index_register));
            }
            negative_index = signed_index < 0;
            index_magnitude = negative_index
                ? static_cast<std::uint64_t>(-(signed_index + 1)) + 1U
                : static_cast<std::uint64_t>(signed_index);
        }

        const unsigned shift = scaled ? size : 0U;
        if (index_magnitude >
            (std::numeric_limits<std::uint64_t>::max() >> shift)) {
            return StepResult::MemoryFault;
        }
        const std::uint64_t offset = index_magnitude << shift;
        const std::uint64_t base = read_base_register(context, base_register);
        std::uint64_t address = base;
        if (negative_index) {
            if (offset > base) return StepResult::MemoryFault;
            address = base - offset;
        } else {
            if (base > std::numeric_limits<std::uint64_t>::max() - offset)
                return StepResult::MemoryFault;
            address = base + offset;
        }

        const bool store = opc == 0U;
        const bool zero_extend_load = opc == 1U;
        const bool sign_extend_to_x = opc == 2U;
        const bool sign_extend_to_w = opc == 3U;
        const std::size_t transfer_size = std::size_t{1} << size;
        std::array<std::uint8_t, 8U> bytes{};
        if (store) {
            const std::uint64_t value =
                read_register(context, target) & width_mask(size == 3U);
            for (std::size_t byte = 0U; byte < transfer_size; ++byte)
                bytes[byte] =
                    static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU);
            if (!memory.write(address, std::span<const std::uint8_t>(
                    bytes.data(), transfer_size)))
                return StepResult::MemoryFault;
        } else {
            if (!memory.read(address, std::span<std::uint8_t>(
                    bytes.data(), transfer_size)))
                return StepResult::MemoryFault;
            std::uint64_t value = 0U;
            for (std::size_t byte = 0U; byte < transfer_size; ++byte)
                value |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
            if (sign_extend_to_x || sign_extend_to_w) {
                value = static_cast<std::uint64_t>(
                    sign_extend(value, static_cast<unsigned>(transfer_size * 8U)));
                write_register(context, target, value, sign_extend_to_x);
            } else {
                write_register(context, target, value, zero_extend_load && size == 3U);
            }
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Unscaled, pre-index and post-index scalar integer loads/stores.
    // Unprivileged, register-offset and SIMD/FP forms remain unsupported.
    if ((instruction & 0x3B200000U) == 0x38000000U) {
        if ((instruction & 0x04000000U) != 0U)
            return StepResult::UnsupportedInstruction;
        const unsigned size = (instruction >> 30U) & 0x3U;
        const unsigned opc = (instruction >> 22U) & 0x3U;
        const unsigned mode = (instruction >> 10U) & 0x3U;
        const unsigned target = instruction & 0x1FU;
        const unsigned base_register = (instruction >> 5U) & 0x1FU;
        const std::int64_t displacement =
            sign_extend((instruction >> 12U) & 0x1FFU, 9U);
        const bool store = opc == 0U;
        const bool zero_extend_load = opc == 1U;
        const bool sign_extend_to_x = opc == 2U;
        const bool sign_extend_to_w = opc == 3U;
        if (mode == 2U || (store && opc != 0U) ||
            ((sign_extend_to_x || sign_extend_to_w) && size == 3U) ||
            (sign_extend_to_w && size > 1U)) {
            return StepResult::UnsupportedInstruction;
        }
        const std::size_t transfer_size = std::size_t{1} << size;
        const std::uint64_t base = read_base_register(context, base_register);
        std::uint64_t address = base;
        std::uint64_t writeback_value = base;
        const bool writeback = mode == 1U || mode == 3U;
        if (mode == 0U) {
            if (displacement < 0) {
                const std::uint64_t magnitude = static_cast<std::uint64_t>(-displacement);
                if (magnitude > base) return StepResult::MemoryFault;
                address = base - magnitude;
            } else {
                const std::uint64_t amount = static_cast<std::uint64_t>(displacement);
                if (base > std::numeric_limits<std::uint64_t>::max() - amount)
                    return StepResult::MemoryFault;
                address = base + amount;
            }
        } else if (mode == 3U) {
            if (displacement < 0) {
                const std::uint64_t magnitude = static_cast<std::uint64_t>(-displacement);
                if (magnitude > base) return StepResult::MemoryFault;
                address = base - magnitude;
            } else {
                const std::uint64_t amount = static_cast<std::uint64_t>(displacement);
                if (base > std::numeric_limits<std::uint64_t>::max() - amount)
                    return StepResult::MemoryFault;
                address = base + amount;
            }
            writeback_value = address;
        } else { // post-index
            address = base;
            if (displacement < 0) {
                const std::uint64_t magnitude = static_cast<std::uint64_t>(-displacement);
                if (magnitude > base) return StepResult::MemoryFault;
                writeback_value = base - magnitude;
            } else {
                const std::uint64_t amount = static_cast<std::uint64_t>(displacement);
                if (base > std::numeric_limits<std::uint64_t>::max() - amount)
                    return StepResult::MemoryFault;
                writeback_value = base + amount;
            }
        }
        if (writeback && base_register != 31U && base_register == target)
            return StepResult::UnsupportedInstruction;

        std::array<std::uint8_t, 8U> bytes{};
        if (store) {
            const std::uint64_t value =
                read_register(context, target) & width_mask(size == 3U);
            for (std::size_t byte = 0U; byte < transfer_size; ++byte)
                bytes[byte] =
                    static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU);
            const auto data = std::span<const std::uint8_t>(
                bytes.data(), transfer_size);
            if (!memory.write(address, data)) return StepResult::MemoryFault;
        } else {
            const auto data = std::span<std::uint8_t>(
                bytes.data(), transfer_size);
            if (!memory.read(address, data)) return StepResult::MemoryFault;
            std::uint64_t value = 0U;
            for (std::size_t byte = 0U; byte < transfer_size; ++byte)
                value |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
            if (sign_extend_to_x || sign_extend_to_w) {
                const unsigned bits = static_cast<unsigned>(transfer_size * 8U);
                value = static_cast<std::uint64_t>(sign_extend(value, bits));
                write_register(context, target, value, sign_extend_to_x);
            } else {
                write_register(context, target, value, zero_extend_load && size == 3U);
            }
        }
        if (writeback) {
            if (base_register == 31U) context.sp = writeback_value;
            else context.x[base_register] = writeback_value;
        }
        context.program_counter = next_pc;
        ++context.instructions_retired;
        return StepResult::Executed;
    }

    // Unsigned-immediate scalar LDR/STR, including SIMD/FP S/D/Q registers.
    if ((instruction & 0x3B000000U) == 0x39000000U) {
        if ((instruction & 0x04000000U) != 0U) {
            const unsigned size = (instruction >> 30U) & 0x3U;
            const unsigned opc = (instruction >> 22U) & 0x3U;
            std::size_t transfer_size = 0U;
            unsigned scale = 0U;
            if (size == 2U && opc <= 1U) { transfer_size = 4U; scale = 2U; }
            else if (size == 3U && opc <= 1U) { transfer_size = 8U; scale = 3U; }
            else if (size == 0U && (opc == 2U || opc == 3U)) {
                transfer_size = 16U; scale = 4U;
            } else {
                return StepResult::UnsupportedInstruction;
            }
            const bool load = (opc & 1U) != 0U;
            const unsigned target = instruction & 0x1FU;
            const unsigned base_register = (instruction >> 5U) & 0x1FU;
            const std::uint64_t base = read_base_register(context, base_register);
            const std::uint64_t offset =
                static_cast<std::uint64_t>((instruction >> 10U) & 0xFFFU) << scale;
            if (base > std::numeric_limits<std::uint64_t>::max() - offset)
                return StepResult::MemoryFault;
            const std::uint64_t address = base + offset;
            std::array<std::uint8_t, 16U> bytes{};
            if (load) {
                if (!memory.read(address, std::span<std::uint8_t>(
                        bytes.data(), transfer_size)))
                    return StepResult::MemoryFault;
                std::uint64_t low = 0U;
                std::uint64_t high = 0U;
                for (std::size_t byte = 0U; byte <
                     std::min<std::size_t>(transfer_size, 8U); ++byte)
                    low |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
                if (transfer_size == 16U) {
                    for (std::size_t byte = 0U; byte < 8U; ++byte)
                        high |= static_cast<std::uint64_t>(
                            bytes[8U + byte]) << (byte * 8U);
                }
                context.v[target] = {low, high};
            } else {
                const std::uint64_t low = context.v[target].low;
                for (std::size_t byte = 0U; byte <
                     std::min<std::size_t>(transfer_size, 8U); ++byte)
                    bytes[byte] = static_cast<std::uint8_t>(
                        (low >> (byte * 8U)) & 0xFFU);
                if (transfer_size == 16U) {
                    const std::uint64_t high = context.v[target].high;
                    for (std::size_t byte = 0U; byte < 8U; ++byte)
                        bytes[8U + byte] = static_cast<std::uint8_t>(
                            (high >> (byte * 8U)) & 0xFFU);
                }
                if (!memory.write(address, std::span<const std::uint8_t>(
                        bytes.data(), transfer_size)))
                    return StepResult::MemoryFault;
            }
            context.program_counter = next_pc;
            ++context.instructions_retired;
            return StepResult::Executed;
        }
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
        if ((instruction & 0x80000000U) != 0U)
            write_register(context, 30U, next_pc, true);
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
    if (branch_class == 0xD61F0000U ||
        branch_class == 0xD63F0000U ||
        branch_class == 0xD65F0000U) {
        const unsigned source = (instruction >> 5U) & 0x1FU;
        if (branch_class == 0xD63F0000U)
            write_register(context, 30U, next_pc, true);
        context.program_counter = read_register(context, source);
        ++context.instructions_retired;
        return StepResult::Branch;
    }

    return StepResult::UnsupportedInstruction;
}

} // namespace lucid::cpu
