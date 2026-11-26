#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lucid::switchfmt {

inline constexpr std::uint32_t MaximumNspEntryCount = 8192U;
inline constexpr std::uint32_t MaximumNspStringTableSize = 16U * 1024U * 1024U;
inline constexpr std::size_t Pfs0HeaderSize = 0x10U;
inline constexpr std::size_t Pfs0EntrySize = 0x18U;

struct NspFileEntry {
    std::string name;
    std::uint64_t offset{};
    std::uint64_t size{};
};

struct NspPackageInfo {
    std::uint64_t package_size{};
    std::uint64_t pfs0_data_offset{};
    std::uint32_t file_count{};
    std::uint32_t string_table_size{};
    std::uint32_t nca_count{};
    std::uint32_t ticket_count{};
    std::uint32_t certificate_count{};
    std::uint32_t metadata_nca_count{};
    std::uint32_t other_file_count{};
    std::vector<NspFileEntry> files;
    bool contains_nca_content{};
    bool launchable_by_current_engine{};
};

// Parses only the PFS0 directory embedded in an NSP. Does not read, extract,
// decrypt, or execute NCA content and never inspects title-key material.
[[nodiscard]] bool inspect_nsp_file(const std::filesystem::path& path,
                                    NspPackageInfo& output,
                                    std::string* error = nullptr);

} // namespace lucid::switchfmt
