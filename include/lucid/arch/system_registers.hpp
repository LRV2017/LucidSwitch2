#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace lucid::arch {
enum class SystemRegister : std::uint8_t { CurrentEL, DAIF, SPSel, ELR_EL1, ESR_EL1, FAR_EL1, VBAR_EL1, SCTLR_EL1, TTBR0_EL1, TCR_EL1, MAIR_EL1, Count };
enum class RegisterAccess { Ok, ReadOnly, Invalid };
class SystemRegisters final {
public:
 std::uint64_t read(SystemRegister reg) const noexcept;
 RegisterAccess write(SystemRegister reg,std::uint64_t value) noexcept;
 bool valid(SystemRegister reg) const noexcept;
private: std::array<std::uint64_t,static_cast<std::size_t>(SystemRegister::Count)> values_{};
};
[[nodiscard]] bool decode_system_register(std::uint16_t encoding,
                                           SystemRegister& reg) noexcept;
enum class ExceptionKind : std::uint8_t { Synchronous, IRQ, FIQ, SError };
struct ExceptionRecord { ExceptionKind kind{ExceptionKind::Synchronous}; std::uint8_t source_el{}; std::uint64_t return_address{},syndrome{},fault_address{}; };
class ExceptionRouter final { public: std::uint64_t enter(SystemRegisters&,const ExceptionRecord&) const noexcept; };
}