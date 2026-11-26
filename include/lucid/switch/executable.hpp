#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace lucid::switchfmt {

inline constexpr std::size_t NroHeaderSize = 0x80U;
inline constexpr std::size_t NsoHeaderSize = 0x100U;
inline constexpr std::uint64_t MaximumInputSize = 256ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t MaximumSegmentSize = 128ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t MaximumMemoryImageSize = 512ULL * 1024ULL * 1024ULL;

enum class ExecutableFormat : std::uint8_t { Nro0, Nso0 };
enum class SegmentKind : std::uint8_t { Text, ReadOnlyData, Data };

struct SegmentInfo {
    SegmentKind kind{SegmentKind::Text};
    std::uint32_t file_offset{};
    std::uint32_t memory_offset{};
    std::uint32_t stored_size{};
    std::uint32_t memory_size{};
    bool compressed{};
    bool hash_required{};
    bool execute_only{};
};

struct ExecutableInfo {
    ExecutableFormat format{ExecutableFormat::Nro0};
    std::uint32_t version{};
    std::uint32_t flags{};
    std::uint64_t input_size{};
    std::uint32_t declared_image_size{};
    std::uint32_t bss_size{};
    std::uint32_t entry_stub_instruction{};
    std::uint32_t module_header_location{};
    std::uint32_t additional_header_offset{};
    std::array<std::uint8_t, 32U> build_id{};
    std::array<SegmentInfo, 3U> segments{};
    bool has_asset_section{};
    bool uses_zstd_compression{};
    std::uint64_t trailing_bytes{};
};

// Inspects only public, plaintext NRO0/NSO0 metadata and segment ranges.
// It does not decrypt, decompress, relocate, load, or execute the program.
[[nodiscard]] bool inspect_executable(std::span<const std::uint8_t> bytes,
                                      ExecutableInfo& output,
                                      std::string* error = nullptr);

[[nodiscard]] bool inspect_executable_file(const std::filesystem::path& path,
                                           ExecutableInfo& output,
                                           std::string* error = nullptr);

[[nodiscard]] std::string_view to_string(ExecutableFormat format) noexcept;
[[nodiscard]] std::string_view to_string(SegmentKind kind) noexcept;

} // namespace lucid::switchfmt
