#pragma once

#include "lucid/core/memory_bus.hpp"

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
};

enum class StepResult {
    Executed,
    Branch,
    Halted,
    UnsupportedInstruction,
    MemoryFault,
    MisalignedProgramCounter
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

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Lucid A64 subset interpreter";
    }
};

} // namespace lucid::cpu
