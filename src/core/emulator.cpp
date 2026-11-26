#include "lucid/core/emulator.hpp"

#include <array>
#include <limits>

namespace lucid::core {
namespace {

constexpr std::uint64_t kTableDescriptor = 3U;
constexpr std::uint64_t kAccessFlag = 1ULL << 10U;
constexpr std::uint64_t kBlockDescriptor = 1U;
constexpr std::uint64_t kBlockAddressMask = 0x0000FFFFC0000000ULL;

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

[[nodiscard]] bool address_in_region(std::uint64_t address, std::uint64_t base,
                                     std::size_t size) noexcept {
    return address >= base &&
        address - base < static_cast<std::uint64_t>(size);
}

[[nodiscard]] bool write_descriptor(MemoryBus& memory,
                                    const std::uint64_t address,
                                    const std::uint64_t descriptor) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            (descriptor >> (index * 8U)) & 0xFFU);
    }
    return memory.write(address, bytes);
}

// Build a minimal identity map with an L0 table and L1 1 GiB block entries.
// The physical MemoryBus remains the final guard against accesses outside the
// configured guest backing store.
[[nodiscard]] bool build_identity_tables(
    MemoryBus& memory, const std::uint64_t guest_base,
    const std::uint64_t guest_size, std::uint64_t& root_address,
    std::string* error) {
    constexpr std::uint64_t page_size = mmu::Stage1Mmu::PageSize;
    const std::uint64_t last_address = guest_base + guest_size - 1U;
    if ((last_address >> 48U) != 0U) {
        set_error(error, "The initial stage-1 bootstrap supports lower 48-bit guest addresses only.");
        return false;
    }

    const std::uint64_t first_l0 = guest_base >> 39U;
    const std::uint64_t last_l0 = last_address >> 39U;
    const std::uint64_t l0_count = last_l0 - first_l0 + 1U;
    if (guest_size > std::numeric_limits<std::uint64_t>::max() - guest_base ||
        guest_base + guest_size > std::numeric_limits<std::uint64_t>::max() - (page_size - 1U)) {
        set_error(error, "The stage-1 table allocation address overflows.");
        return false;
    }

    const std::uint64_t table_base =
        (guest_base + guest_size + page_size - 1U) & ~(page_size - 1U);
    const std::uint64_t table_bytes64 = (l0_count + 1U) * page_size;
    if ((table_base >> 48U) != 0U) {
        set_error(error, "The stage-1 page tables must fit in the supported 48-bit physical address range.");
        return false;
    }
    if (table_bytes64 > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        set_error(error, "The stage-1 table allocation is too large for this host.");
        return false;
    }
    const auto table_bytes = static_cast<std::size_t>(table_bytes64);
    if (!memory.map_region(table_base, table_bytes, error,
                           MemoryPermission::ReadWrite)) {
        return false;
    }

    root_address = table_base;
    for (std::uint64_t l0 = first_l0; l0 <= last_l0; ++l0) {
        const std::uint64_t l1_table =
            table_base + (1U + (l0 - first_l0)) * page_size;
        if (!write_descriptor(memory, root_address + l0 * 8U,
                              l1_table | kTableDescriptor)) {
            set_error(error, "Could not write the stage-1 L0 table descriptor.");
            return false;
        }
    }

    const std::uint64_t first_block = guest_base >> 30U;
    const std::uint64_t last_block = last_address >> 30U;
    for (std::uint64_t block = first_block; block <= last_block; ++block) {
        const std::uint64_t l0 = block >> 9U;
        const std::uint64_t l1_index = block & 0x1FFU;
        const std::uint64_t l1_table =
            table_base + (1U + (l0 - first_l0)) * page_size;
        const std::uint64_t physical_base = block << 30U;
        const std::uint64_t descriptor =
            (physical_base & kBlockAddressMask) |
            kAccessFlag | kBlockDescriptor;
        if (!write_descriptor(memory, l1_table + l1_index * 8U, descriptor)) {
            set_error(error, "Could not write the stage-1 L1 block descriptor.");
            return false;
        }
    }
    return true;
}

} // namespace

Emulator::Emulator() : stage1_mmu_(memory_) {}

bool Emulator::initialize(const EmulatorConfig& config, std::string* error) {
    if (state_ != SessionState::Cold) {
        set_error(error, "The emulator session has already been initialized.");
        return false;
    }
    if (config.memory_size == 0U) {
        set_error(error, "The configured guest memory size must be nonzero.");
        return false;
    }
    const auto size64 = static_cast<std::uint64_t>(config.memory_size);
    if (size64 > std::numeric_limits<std::uint64_t>::max() - config.memory_base) {
        set_error(error, "The configured guest memory range overflows.");
        return false;
    }
    if ((config.entry_point & 3U) != 0U ||
        !address_in_region(config.entry_point, config.memory_base, config.memory_size)) {
        set_error(error, "The entry point must be aligned and inside guest memory.");
        return false;
    }
    if (!memory_.map_region(config.memory_base, config.memory_size, error)) return false;

    std::uint64_t root_address = 0U;
    if (!build_identity_tables(memory_, config.memory_base, size64,
                               root_address, error)) {
        return false;
    }
    if (!stage1_mmu_.configure(root_address, mmu::Stage1Mmu::DefaultTcr)) {
        set_error(error, "Could not configure the supported 4 KiB / 48-bit stage-1 regime.");
        return false;
    }

    config_ = config;
    cpu_context_ = {};
    cpu_context_.program_counter = config.entry_point;
    (void)cpu_context_.system_registers.write(
        arch::SystemRegister::TTBR0_EL1, root_address);
    (void)cpu_context_.system_registers.write(
        arch::SystemRegister::TCR_EL1, mmu::Stage1Mmu::DefaultTcr);
    (void)cpu_context_.system_registers.write(
        arch::SystemRegister::SCTLR_EL1, 1U);
    last_step_result_ = cpu::StepResult::Executed;
    state_ = SessionState::Ready;
    return true;
}

bool Emulator::load_program(std::uint64_t address,
    std::span<const std::uint8_t> bytes, std::string* error) {
    if (state_ != SessionState::Ready) {
        set_error(error, "Programs can only be loaded into a ready session.");
        return false;
    }
    if (bytes.empty()) {
        set_error(error, "The program image cannot be empty.");
        return false;
    }
    return memory_.write(address, bytes, error);
}

RunResult Emulator::run(std::uint64_t instruction_budget) noexcept {
    if (state_ == SessionState::Cold) return RunResult::NotInitialized;
    if (state_ != SessionState::Ready && state_ != SessionState::Paused)
        return RunResult::InvalidState;
    if (instruction_budget == 0U) return RunResult::InvalidState;
    state_ = SessionState::Running;
    for (std::uint64_t count = 0; count < instruction_budget; ++count) {
        cpu::CpuMemory execution_memory(memory_, stage1_mmu_);
        last_step_result_ = cpu_.step(cpu_context_, execution_memory);
        if (last_step_result_ != cpu::StepResult::Executed &&
            last_step_result_ != cpu::StepResult::Branch) {
            state_ = SessionState::Faulted;
            return RunResult::CpuFault;
        }
    }
    state_ = SessionState::Paused;
    return RunResult::InstructionLimitReached;
}

bool Emulator::stop() noexcept {
    if (state_ != SessionState::Ready && state_ != SessionState::Paused) return false;
    state_ = SessionState::Stopped;
    return true;
}
SessionState Emulator::state() const noexcept { return state_; }
const cpu::CpuContext& Emulator::cpu_context() const noexcept { return cpu_context_; }
cpu::CpuContext& Emulator::cpu_context() noexcept { return cpu_context_; }
const MemoryBus& Emulator::memory_bus() const noexcept { return memory_; }
MemoryBus& Emulator::memory_bus() noexcept { return memory_; }
const mmu::Stage1Mmu& Emulator::stage1_mmu() const noexcept { return stage1_mmu_; }
mmu::Stage1Mmu& Emulator::stage1_mmu() noexcept { return stage1_mmu_; }
cpu::StepResult Emulator::last_step_result() const noexcept { return last_step_result_; }

const char* to_string(SessionState state) noexcept {
    switch (state) {
    case SessionState::Cold: return "cold";
    case SessionState::Ready: return "ready";
    case SessionState::Running: return "running";
    case SessionState::Paused: return "paused";
    case SessionState::Stopped: return "stopped";
    case SessionState::Faulted: return "faulted";
    }
    return "unknown";
}
const char* to_string(RunResult result) noexcept {
    switch (result) {
    case RunResult::InstructionLimitReached: return "instruction limit reached";
    case RunResult::NotInitialized: return "not initialized";
    case RunResult::InvalidState: return "invalid state or instruction budget";
    case RunResult::CpuFault: return "CPU execution fault";
    }
    return "unknown";
}
const char* to_string(cpu::StepResult result) noexcept {
    switch (result) {
    case cpu::StepResult::Executed: return "executed";
    case cpu::StepResult::Branch: return "branch";
    case cpu::StepResult::Halted: return "halted";
    case cpu::StepResult::UnsupportedInstruction: return "unsupported instruction";
    case cpu::StepResult::MemoryFault: return "memory fault";
    case cpu::StepResult::MisalignedProgramCounter: return "misaligned program counter";
    case cpu::StepResult::SystemRegisterFault: return "system register access fault";
    }
    return "unknown";
}
} // namespace lucid::core
