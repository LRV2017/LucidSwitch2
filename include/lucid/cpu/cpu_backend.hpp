#pragma once

#include "lucid/arch/system_registers.hpp"
#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_memory.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace lucid::cpu {

struct CpuFlags {
    bool negative{};
    bool zero{};
    bool carry{};
    bool overflow{};
};

struct CpuContext {
    std::array<std::uint64_t, 31> x{};
    std::uint64_t sp{};
    std::uint64_t program_counter{};
    std::uint64_t instructions_retired{};
    CpuFlags flags{};
    arch::SystemRegisters system_registers{};

    [[nodiscard]] std::uint64_t pstate() const noexcept {
        const auto el = system_registers.current_el();
        const auto mode = (static_cast<std::uint64_t>(el) << 2U) |
            (el == 1U ? (system_registers.read(
                arch::SystemRegister::SPSel) & 1U) : 0U);
        const std::uint64_t nzcv =
            (flags.negative ? (1ULL << 31U) : 0U) |
            (flags.zero ? (1ULL << 30U) : 0U) |
            (flags.carry ? (1ULL << 29U) : 0U) |
            (flags.overflow ? (1ULL << 28U) : 0U);
        return nzcv |
            (system_registers.read(arch::SystemRegister::DAIF) & 0x3C0U) |
            mode;
    }

    void restore_nzcv(const std::uint64_t saved_pstate) noexcept {
        flags.negative = (saved_pstate & (1ULL << 31U)) != 0U;
        flags.zero = (saved_pstate & (1ULL << 30U)) != 0U;
        flags.carry = (saved_pstate & (1ULL << 29U)) != 0U;
        flags.overflow = (saved_pstate & (1ULL << 28U)) != 0U;
    }
};

enum class StepResult {
    Executed,
    Branch,
    Halted,
    UnsupportedInstruction,
    MemoryFault,
    MisalignedProgramCounter,
    SystemRegisterFault,
    ExceptionTaken
};

// A deliberately incomplete A64 interpreter. It implements only the
// instructions listed in docs/a64-subset.md; it is not a console CPU core.
class CpuBackend {
public:
    virtual ~CpuBackend() = default;
    [[nodiscard]] virtual StepResult step(
        CpuContext& context,
        core::MemoryBus& memory
    ) = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

class A64Interpreter final : public CpuBackend {
public:
    [[nodiscard]] StepResult step(
        CpuContext& context,
        core::MemoryBus& memory
    ) override;

    [[nodiscard]] StepResult step(CpuContext& context, CpuMemory& memory);

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Lucid A64 subset interpreter";
    }
};

} // namespace lucid::cpu
