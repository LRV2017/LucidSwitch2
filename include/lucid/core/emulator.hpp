#pragma once
#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"
#include "lucid/mmu/stage1_mmu.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace lucid::core {
enum class SessionState { Cold, Ready, Running, Paused, Stopped, Faulted };
enum class RunResult { InstructionLimitReached, NotInitialized, InvalidState, CpuFault };
struct EmulatorConfig {
    std::uint64_t memory_base{0x1000U};
    std::size_t memory_size{1024U * 1024U};
    std::uint64_t entry_point{0x1000U};
};
class Emulator final {
public:
    Emulator();
    [[nodiscard]] bool initialize(const EmulatorConfig& config = {},
                                  std::string* error = nullptr);
    [[nodiscard]] bool load_program(std::uint64_t address,
        std::span<const std::uint8_t> bytes, std::string* error = nullptr);
    // Runs at most the requested number of instructions. Budget exhaustion pauses;
    // calling run again resumes from the current CPU state.
    [[nodiscard]] RunResult run(std::uint64_t instruction_budget = 100000U) noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] SessionState state() const noexcept;
    [[nodiscard]] const cpu::CpuContext& cpu_context() const noexcept;
    [[nodiscard]] cpu::CpuContext& cpu_context() noexcept;
    [[nodiscard]] const MemoryBus& memory_bus() const noexcept;
    [[nodiscard]] MemoryBus& memory_bus() noexcept;
    [[nodiscard]] const mmu::Stage1Mmu& stage1_mmu() const noexcept;
    [[nodiscard]] mmu::Stage1Mmu& stage1_mmu() noexcept;
    [[nodiscard]] cpu::StepResult last_step_result() const noexcept;
private:
    SessionState state_{SessionState::Cold};
    EmulatorConfig config_{};
    MemoryBus memory_{};
    mmu::Stage1Mmu stage1_mmu_;
    cpu::A64Interpreter cpu_{};
    cpu::CpuContext cpu_context_{};
    cpu::StepResult last_step_result_{cpu::StepResult::Executed};
};
[[nodiscard]] const char* to_string(SessionState state) noexcept;
[[nodiscard]] const char* to_string(RunResult result) noexcept;
[[nodiscard]] const char* to_string(cpu::StepResult result) noexcept;
} // namespace lucid::core
