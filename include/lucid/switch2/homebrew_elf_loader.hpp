#pragma once

#include "lucid/core/memory_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace lucid::switch2 {

struct HomebrewElfLoadInfo {
    std::uint64_t entry_point{};
    std::size_t loaded_segments{};
    std::uint64_t total_memory_bytes{};
};

// Loads ordinary static ELF64 little-endian AArch64 ET_EXEC homebrew images.
// This intentionally does not parse console containers, tickets, NCAs,
// protected executable formats, dynamic linking, or platform-specific ABI data.
class HomebrewElfLoader final {
public:
    static constexpr std::size_t MaxProgramHeaders = 128U;
    static constexpr std::uint64_t MaxSegmentBytes = 256ULL * 1024ULL * 1024ULL;
    static constexpr std::uint64_t MaxImageMemoryBytes = 512ULL * 1024ULL * 1024ULL;
    static constexpr std::uint64_t MaxGuestVirtualAddress = 1ULL << 48U;

    [[nodiscard]] bool load(std::span<const std::uint8_t> image,
                            core::MemoryBus& memory,
                            HomebrewElfLoadInfo& info,
                            std::string* error = nullptr) const;
};

} // namespace lucid::switch2
