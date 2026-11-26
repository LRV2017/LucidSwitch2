#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace lucid::homebrew {

inline constexpr std::uint16_t PackageVersion = 1U;
inline constexpr std::size_t PackageHeaderSize = 48U;
inline constexpr std::uint64_t DefaultLoadAddress = 0x10000U;
inline constexpr std::size_t MaximumPackageFileSize = 16U * 1024U * 1024U;
inline constexpr std::uint64_t MaximumGuestFootprint = 64U * 1024U * 1024U;

// LXP is an original, little-endian test-program container. It is not a
// console executable format and carries no platform-specific loader metadata.
struct ProgramPackage {
    std::uint64_t load_address{DefaultLoadAddress};
    std::uint32_t entry_offset{};
    std::uint32_t bss_size{};
    std::uint32_t stack_size{64U * 1024U};
    std::vector<std::uint8_t> code;
    std::vector<std::uint8_t> data;
};

[[nodiscard]] bool encode_program(const ProgramPackage& program,
                                  std::vector<std::uint8_t>& output,
                                  std::string* error = nullptr);
[[nodiscard]] bool decode_program(std::span<const std::uint8_t> bytes,
                                  ProgramPackage& output,
                                  std::string* error = nullptr);
[[nodiscard]] bool read_program_file(const std::filesystem::path& path,
                                     ProgramPackage& output,
                                     std::string* error = nullptr);
[[nodiscard]] bool write_program_file(const std::filesystem::path& path,
                                      const ProgramPackage& program,
                                      std::string* error = nullptr);

} // namespace lucid::homebrew
