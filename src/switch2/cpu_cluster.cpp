#include "lucid/switch2/cpu_cluster.hpp"

namespace lucid::switch2 {

bool CpuCluster::start_core(const std::size_t core_index,
                            const std::uint64_t entry_point,
                            const std::uint64_t stack_pointer) noexcept {
    if (core_index >= CoreCount || (entry_point & 3U) != 0U) return false;
    CoreSlot& slot = cores_[core_index];
    if (slot.state != CpuCoreState::Parked) return false;

    slot.context = {};
    slot.context.program_counter = entry_point;
    slot.context.sp = stack_pointer;
    slot.last_result = cpu::StepResult::Executed;
    slot.state = CpuCoreState::Runnable;
    return true;
}

bool CpuCluster::step(const std::size_t core_index,
                      core::MemoryBus& memory,
                      cpu::StepResult& result) {
    if (core_index >= CoreCount) return false;
    CoreSlot& slot = cores_[core_index];
    if (slot.state != CpuCoreState::Runnable) return false;

    result = interpreter_.step(slot.context, memory);
    slot.last_result = result;
    switch (result) {
        case cpu::StepResult::Executed:
        case cpu::StepResult::Branch:
        case cpu::StepResult::ExceptionTaken:
            break;
        case cpu::StepResult::Halted:
            slot.state = CpuCoreState::Halted;
            break;
        case cpu::StepResult::UnsupportedInstruction:
        case cpu::StepResult::MemoryFault:
        case cpu::StepResult::MisalignedProgramCounter:
        case cpu::StepResult::SystemRegisterFault:
            slot.state = CpuCoreState::Faulted;
            break;
    }
    return true;
}

CpuClusterRunReport CpuCluster::run_round_robin(
    core::MemoryBus& memory,
    const std::uint64_t instruction_budget) {
    CpuClusterRunReport report{};
    for (std::uint64_t quantum = 0; quantum < instruction_budget; ++quantum) {
        std::size_t selected = CoreCount;
        for (std::size_t offset = 0; offset < CoreCount; ++offset) {
            const std::size_t candidate = (next_core_ + offset) % CoreCount;
            if (cores_[candidate].state == CpuCoreState::Runnable) {
                selected = candidate;
                break;
            }
        }
        if (selected == CoreCount) break;

        CoreSlot& slot = cores_[selected];
        const std::uint64_t before = slot.context.instructions_retired;
        cpu::StepResult result = cpu::StepResult::Executed;
        if (step(selected, memory, result) &&
            slot.context.instructions_retired > before) {
            report.instructions_executed +=
                slot.context.instructions_retired - before;
        }
        next_core_ = (selected + 1U) % CoreCount;
    }

    report.runnable_cores = count_state(CpuCoreState::Runnable);
    report.halted_cores = count_state(CpuCoreState::Halted);
    report.faulted_cores = count_state(CpuCoreState::Faulted);
    report.next_core = next_core_;
    return report;
}

const cpu::CpuContext* CpuCluster::context(
    const std::size_t core_index) const noexcept {
    return core_index < CoreCount ? &cores_[core_index].context : nullptr;
}

CpuCoreState CpuCluster::state(const std::size_t core_index) const noexcept {
    return core_index < CoreCount ? cores_[core_index].state : CpuCoreState::Faulted;
}

cpu::StepResult CpuCluster::last_step_result(
    const std::size_t core_index) const noexcept {
    return core_index < CoreCount
        ? cores_[core_index].last_result
        : cpu::StepResult::UnsupportedInstruction;
}

std::size_t CpuCluster::next_core() const noexcept { return next_core_; }

std::size_t CpuCluster::count_state(const CpuCoreState wanted) const noexcept {
    std::size_t count = 0U;
    for (const CoreSlot& slot : cores_) {
        if (slot.state == wanted) ++count;
    }
    return count;
}

const char* to_string(const CpuCoreState state) noexcept {
    switch (state) {
        case CpuCoreState::Parked: return "parked";
        case CpuCoreState::Runnable: return "runnable";
        case CpuCoreState::Halted: return "halted";
        case CpuCoreState::Faulted: return "faulted";
    }
    return "unknown";
}

} // namespace lucid::switch2
