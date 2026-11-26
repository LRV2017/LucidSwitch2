#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lucid::arch {

enum class SystemRegister : std::uint8_t {
    CurrentEL,
    DAIF,
    SPSel,
    ELR_EL1,
    ESR_EL1,
    FAR_EL1,
    SPSR_EL1,
    VBAR_EL1,
    SCTLR_EL1,
    TTBR0_EL1,
    TCR_EL1,
    MAIR_EL1,
    Count
};

enum class RegisterAccess {
    Ok,
    ReadOnly,
    Invalid
};

class SystemRegisters final {
public:
    [[nodiscard]] std::uint64_t read(SystemRegister reg) const noexcept;
    [[nodiscard]] RegisterAccess write(SystemRegister reg,
                                       std::uint64_t value) noexcept;
    [[nodiscard]] bool valid(SystemRegister reg) const noexcept;

    // CurrentEL is read-only to guest instructions. This internal architectural
    // hook is used only when the exception model changes privilege level.
    void set_exception_level(std::uint8_t level) noexcept;
    [[nodiscard]] std::uint8_t current_el() const noexcept;

private:
    std::array<std::uint64_t,
        static_cast<std::size_t>(SystemRegister::Count)> values_{};
};

[[nodiscard]] bool decode_system_register(std::uint16_t encoding,
                                         SystemRegister& reg) noexcept;

enum class ExceptionKind : std::uint8_t {
    Synchronous,
    IRQ,
    FIQ,
    SError
};

struct ExceptionRecord {
    ExceptionKind kind{ExceptionKind::Synchronous};
    std::uint8_t source_el{};
    std::uint64_t return_address{};
    std::uint64_t syndrome{};
    std::uint64_t fault_address{};
    std::uint64_t saved_pstate{};
};

class ExceptionRouter final {
public:
    // Saves EL1 exception state, masks asynchronous exceptions, switches to
    // EL1h, and returns the selected 128-byte vector slot address.
    [[nodiscard]] std::uint64_t enter(SystemRegisters& registers,
                                      const ExceptionRecord& exception) const noexcept;

    // Restores the supported EL0t, EL1t, or EL1h mode from SPSR_EL1.
    [[nodiscard]] bool return_from_exception(SystemRegisters& registers,
                                              std::uint64_t& program_counter) const noexcept;
};

} // namespace lucid::arch
