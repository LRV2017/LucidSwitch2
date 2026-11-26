#pragma once

#include "lucid/core/emulator.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace lucid::switchfmt {

inline constexpr std::uint64_t DefaultNroInstructionBudget = 100000U;
inline constexpr std::uint64_t MaximumNroInstructionBudget = 10000000U;
inline constexpr std::uint64_t NroHomebrewStackSize = 64ULL * 1024ULL;

struct NroHomebrewRunReport {
    std::uint64_t image_base{};
    std::uint64_t entry_point{};
    std::uint64_t stack_pointer{};
    std::uint64_t instruction_budget{};
    std::uint64_t instructions_retired{};
    std::uint64_t register_x0{};
    std::uint64_t relocations_applied{};
    std::uint64_t program_counter{};
    core::RunResult run_result{core::RunResult::NotInitialized};
    cpu::StepResult last_step{cpu::StepResult::Executed};
};

// Runs only a bounded, plaintext, uncompressed NRO homebrew image whose entry
// stub branches directly into its text segment. Supports only relative MOD0
// relocations; no imports, symbol linking, Horizon services, protected content,
// or host-native code execution.
[[nodiscard]] bool run_restricted_nro_homebrew(
    const std::filesystem::path& path,
    std::uint64_t instruction_budget,
    NroHomebrewRunReport& report,
    std::string* error = nullptr);

} // namespace lucid::switchfmt
