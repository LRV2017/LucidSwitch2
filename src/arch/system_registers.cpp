#include "lucid/arch/system_registers.hpp"

namespace lucid::arch {
namespace {
constexpr std::uint64_t kDaifMask = 0x3C0U;
constexpr std::uint64_t kIrqMask = 1U << 7U;
constexpr std::uint64_t kAllAsyncMasks = 0x3C0U;
}

std::uint64_t SystemRegisters::read(const SystemRegister reg) const noexcept {
    const auto index = static_cast<std::size_t>(reg);
    return index < values_.size() ? values_[index] : 0U;
}

bool SystemRegisters::valid(const SystemRegister reg) const noexcept {
    return static_cast<std::size_t>(reg) < values_.size();
}

RegisterAccess SystemRegisters::write(const SystemRegister reg,
                                      const std::uint64_t value) noexcept {
    if (!valid(reg)) {
        return RegisterAccess::Invalid;
    }
    if (reg == SystemRegister::CurrentEL) {
        return RegisterAccess::ReadOnly;
    }
    values_[static_cast<std::size_t>(reg)] = value;
    return RegisterAccess::Ok;
}

void SystemRegisters::set_exception_level(const std::uint8_t level) noexcept {
    if (level <= 3U) {
        values_[static_cast<std::size_t>(SystemRegister::CurrentEL)] =
            static_cast<std::uint64_t>(level) << 2U;
    }
}

std::uint8_t SystemRegisters::current_el() const noexcept {
    return static_cast<std::uint8_t>(
        (read(SystemRegister::CurrentEL) >> 2U) & 0x3U);
}

bool decode_system_register(const std::uint16_t encoding,
                            SystemRegister& reg) noexcept {
    switch (encoding) {
    case 0xC212U: reg = SystemRegister::CurrentEL; return true;
    case 0xDA11U: reg = SystemRegister::DAIF; return true;
    case 0xC210U: reg = SystemRegister::SPSel; return true;
    case 0xC200U: reg = SystemRegister::SPSR_EL1; return true;
    case 0xC201U: reg = SystemRegister::ELR_EL1; return true;
    case 0xC290U: reg = SystemRegister::ESR_EL1; return true;
    case 0xC300U: reg = SystemRegister::FAR_EL1; return true;
    case 0xC600U: reg = SystemRegister::VBAR_EL1; return true;
    case 0xC080U: reg = SystemRegister::SCTLR_EL1; return true;
    case 0xC100U: reg = SystemRegister::TTBR0_EL1; return true;
    case 0xC102U: reg = SystemRegister::TCR_EL1; return true;
    case 0xC510U: reg = SystemRegister::MAIR_EL1; return true;
    default: return false;
    }
}

std::uint64_t ExceptionRouter::enter(
    SystemRegisters& registers, const ExceptionRecord& exception) const noexcept {
    const auto source_el = exception.source_el;
    const bool source_uses_sp_elx =
        (registers.read(SystemRegister::SPSel) & 1U) != 0U;

    (void)registers.write(SystemRegister::ELR_EL1, exception.return_address);
    (void)registers.write(SystemRegister::ESR_EL1, exception.syndrome);
    (void)registers.write(SystemRegister::FAR_EL1, exception.fault_address);
    (void)registers.write(SystemRegister::SPSR_EL1, exception.saved_pstate);

    const auto daif = registers.read(SystemRegister::DAIF);
    (void)registers.write(SystemRegister::DAIF, daif | kAllAsyncMasks);
    (void)registers.write(SystemRegister::SPSel, 1U);
    registers.set_exception_level(1U);

    std::uint64_t base_slot = 0x400U; // Lower EL using AArch64.
    if (source_el == 1U) {
        base_slot = source_uses_sp_elx ? 0x200U : 0x000U;
    }

    std::uint64_t kind_offset = 0U;
    switch (exception.kind) {
    case ExceptionKind::Synchronous: kind_offset = 0x000U; break;
    case ExceptionKind::IRQ: kind_offset = 0x080U; break;
    case ExceptionKind::FIQ: kind_offset = 0x100U; break;
    case ExceptionKind::SError: kind_offset = 0x180U; break;
    }

    const auto vector_base =
        registers.read(SystemRegister::VBAR_EL1) & ~std::uint64_t{0x7FFU};
    return vector_base + base_slot + kind_offset;
}

bool ExceptionRouter::return_from_exception(
    SystemRegisters& registers, std::uint64_t& program_counter) const noexcept {
    const auto saved_pstate = registers.read(SystemRegister::SPSR_EL1);
    const auto mode = static_cast<std::uint8_t>(saved_pstate & 0xFU);
    const bool supported_mode = mode == 0U || mode == 4U || mode == 5U;
    const auto return_address = registers.read(SystemRegister::ELR_EL1);

    if (!supported_mode || (return_address & 3U) != 0U) {
        return false;
    }

    const auto target_el = static_cast<std::uint8_t>((mode >> 2U) & 0x3U);
    const auto target_spsel = static_cast<std::uint64_t>(mode & 1U);
    (void)registers.write(SystemRegister::DAIF, saved_pstate & kDaifMask);
    (void)registers.write(SystemRegister::SPSel, target_spsel);
    registers.set_exception_level(target_el);
    program_counter = return_address;
    return true;
}

} // namespace lucid::arch
