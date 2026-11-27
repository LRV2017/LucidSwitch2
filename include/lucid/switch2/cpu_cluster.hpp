#pragma once

#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lucid::switch2 {

enum class CpuCoreState {
    Parked,
    Runnable,
    Halted,
    Faulted
};

struct CpuClusterRunReport {
    std::uint64_t instructions_executed{};
    std::size_t runnable_cores{};
    std::size_t halted_cores{};
    std::size_t faulted_cores{};
    std::size_t next_core{};
};

// Switch 2 target CPU topology experiment. Each core has independent
// architectural state. A deterministic runner supports repeatable tests;
// run_parallel uses host workers and synchronized shared guest memory.
class CpuCluster final {
public:
    static constexpr std::size_t CoreCount = 8U;

    CpuCluster() = default;

    // Secondary cores start parked. The host-side system model must explicitly
    // start a core with its initial PC and optional stack pointer.
    [[nodiscard]] bool start_core(std::size_t core_index,
                                  std::uint64_t entry_point,
                                  std::uint64_t stack_pointer = 0U) noexcept;

    // Executes exactly one instruction attempt on a runnable core.
    // Returns false when the index is invalid or the core is not runnable.
    [[nodiscard]] bool step(std::size_t core_index,
                            core::MemoryBus& memory,
                            cpu::StepResult& result);

    // Executes up to instruction_budget single-instruction quanta, selecting
    // runnable cores in deterministic round-robin order. A faulted core is
    // isolated; other runnable cores continue.
    [[nodiscard]] CpuClusterRunReport run_round_robin(
        core::MemoryBus& memory,
        std::uint64_t instruction_budget);

    // Execute up to this many instructions on each core that is runnable
    // when the call begins. Worker interleaving is intentionally nondeterministic.
    [[nodiscard]] CpuClusterRunReport run_parallel(
        core::MemoryBus& memory,
        std::uint64_t per_core_instruction_budget);

    [[nodiscard]] const cpu::CpuContext* context(
        std::size_t core_index) const noexcept;
    [[nodiscard]] CpuCoreState state(std::size_t core_index) const noexcept;
    [[nodiscard]] cpu::StepResult last_step_result(
        std::size_t core_index) const noexcept;
    [[nodiscard]] std::size_t next_core() const noexcept;

private:
    struct CoreSlot {
        cpu::CpuContext context{};
        CpuCoreState state{CpuCoreState::Parked};
        cpu::StepResult last_result{cpu::StepResult::Executed};
    };

    [[nodiscard]] std::size_t count_state(CpuCoreState state) const noexcept;

    cpu::A64Interpreter interpreter_{};
    std::array<CoreSlot, CoreCount> cores_{};
    std::size_t next_core_{};
};

[[nodiscard]] const char* to_string(CpuCoreState state) noexcept;

} // namespace lucid::switch2
