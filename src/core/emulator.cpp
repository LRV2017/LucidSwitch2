#include "lucid/core/emulator.hpp"
#include <limits>
namespace lucid::core {
namespace {
void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}
[[nodiscard]] bool address_in_region(std::uint64_t address, std::uint64_t base,
                                     std::size_t size) noexcept {
    return address >= base &&
        address - base < static_cast<std::uint64_t>(size);
}
}
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
    config_ = config;
    cpu_context_ = {};
    cpu_context_.program_counter = config.entry_point;
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
        last_step_result_ = cpu_.step(cpu_context_, memory_);
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
    }
    return "unknown";
}
} // namespace lucid::core
