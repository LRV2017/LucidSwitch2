#include "lucid/arch/svc_dispatcher.hpp"

namespace lucid::arch {
namespace {
constexpr std::uint64_t SvcExceptionClass = 0x15U;
constexpr unsigned ExceptionClassShift = 26U;
constexpr std::uint64_t SvcGetCurrentProcessorNumber = 0x10U;
constexpr std::uint64_t SvcGetSystemTick = 0x1EU;
} // namespace

SvcDispatchResult SvcDispatcher::dispatch(
    cpu::CpuContext& context,
    devices::VirtualTimer& timer,
    const ExceptionRouter& router) const noexcept {
    auto& registers = context.system_registers;
    if (registers.current_el() != 1U) return SvcDispatchResult::NotHandled;

    const std::uint64_t syndrome = registers.read(SystemRegister::ESR_EL1);
    const std::uint64_t exception_class =
        (syndrome >> ExceptionClassShift) & 0x3FU;
    if (exception_class != SvcExceptionClass)
        return SvcDispatchResult::NotHandled;

    const std::uint64_t saved_pstate =
        registers.read(SystemRegister::SPSR_EL1);
    if ((saved_pstate & 0xFU) != 0U)
        return SvcDispatchResult::NotHandled;

    switch (syndrome & 0xFFFFU) {
    case SvcGetCurrentProcessorNumber:
        context.x[0] = 0U; // The current emulator exposes one virtual CPU.
        break;
    case SvcGetSystemTick:
        // The deterministic virtual timer advances one tick per guest
        // instruction; it is not a physical Switch counter-frequency model.
        context.x[0] = timer.counter();
        break;
    default:
        return SvcDispatchResult::NotHandled;
    }

    std::uint64_t return_address =
        registers.read(SystemRegister::ELR_EL1);
    if (!router.return_from_exception(registers, return_address))
        return SvcDispatchResult::InvalidExceptionState;

    context.restore_nzcv(saved_pstate);
    context.program_counter = return_address;
    return SvcDispatchResult::Handled;
}

} // namespace lucid::arch
