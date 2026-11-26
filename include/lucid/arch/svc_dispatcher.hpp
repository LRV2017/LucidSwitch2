#pragma once

#include "lucid/arch/system_registers.hpp"
#include "lucid/cpu/cpu_backend.hpp"
#include "lucid/devices/virtual_timer.hpp"

namespace lucid::arch {

enum class SvcDispatchResult {
    NotHandled,
    Handled,
    InvalidExceptionState
};

// A deliberately small EL0 SVC subset, not a Horizon syscall ABI.
class SvcDispatcher final {
public:
    [[nodiscard]] SvcDispatchResult dispatch(
        cpu::CpuContext& context,
        devices::VirtualTimer& timer,
        const ExceptionRouter& router) const noexcept;
};

} // namespace lucid::arch
