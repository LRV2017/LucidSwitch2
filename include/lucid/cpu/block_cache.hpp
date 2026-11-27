#pragma once
#include "lucid/core/memory_bus.hpp"
#include "lucid/cpu/cpu_backend.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
namespace lucid::cpu {
struct A64BlockCacheStats {
    std::uint64_t blocks_compiled{};
    std::uint64_t cache_hits{};
    std::uint64_t cache_misses{};
    std::uint64_t invalidated_blocks{};
    std::uint64_t instructions_executed{};
};
struct A64BlockRunReport {
    std::uint64_t instructions_executed{};
    StepResult stop_reason{StepResult::Executed};
    bool instruction_budget_exhausted{};
};
// Generation-aware cached interpreter. It caches verified words in short
// basic blocks and uses the existing interpreter; this is not native-code JIT.
class A64BlockCache final {
public:
    static constexpr std::size_t MaxBasicBlockInstructions = 32U;
    [[nodiscard]] A64BlockRunReport run(CpuContext&, core::MemoryBus&, std::uint64_t budget);
    [[nodiscard]] const A64BlockCacheStats& stats() const noexcept;
    void clear() noexcept;
private:
    struct BasicBlock {
        std::uint64_t start_pc{};
        std::uint64_t memory_generation{};
        std::vector<std::uint32_t> instructions;
    };
    [[nodiscard]] static bool terminates_block(std::uint32_t) noexcept;
    [[nodiscard]] BasicBlock* find_or_compile(std::uint64_t, core::MemoryBus&,
                                              std::uint64_t generation);
    A64Interpreter interpreter_{};
    std::unordered_map<std::uint64_t, BasicBlock> blocks_{};
    A64BlockCacheStats stats_{};
};
} // namespace lucid::cpu
