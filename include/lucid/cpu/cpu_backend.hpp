#pragma once

#include <cstdint>
#include <string_view>

namespace lucid::cpu {

struct CpuContext {
    std::uint64_t program_counter{};
    std::uint64_t instructions_retired{};
};

enum class StepResult { Executed, Halted, Unsupported, Fault };

// Contract for a future independently implemented CPU execution engine.
// An interface is not evidence of instruction-set compatibility.
class CpuBackend {
public:
    virtual ~CpuBackend() = default;
    [[nodiscard]] virtual StepResult step(CpuContext& context) = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace lucid::cpu
