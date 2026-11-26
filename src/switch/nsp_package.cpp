#include "lucid/switch/nsp_package.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

namespace lucid::switchfmt {
namespace {

constexpr std::uint64_t kMaximumMetadataSize =
    static_cast<std::uint64_t>(Pfs0HeaderSize) +
    static_cast<std::uint64_t>(MaximumNspEntryCount) * Pfs0EntrySize +
    MaximumNspStringTableSize;

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t read_u64(const std::uint8_t* bytes) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

bool equal_ascii_case_insensitive(const std::string& value,
                                  const char* suffix) noexcept {
    const std::size_t suffix_length = std::char_traits<char>::length(suffix);
    if (value.size() < suffix_length) return false;
    const std::size_t start = value.size() - suffix_length;
    for (std::size_t index = 0U; index < suffix_length; ++index) {
        const auto left = static_cast<unsigned char>(value[start + index]);
        const auto right = static_cast<unsigned char>(suffix[index]);
        if (static_cast<unsigned char>(std::tolower(left)) !=
            static_cast<unsigned char>(std::tolower(right))) return false;
    }
    return true;
}

bool ranges_overlap(const std::uint64_t first_offset,
                    const std::uint64_t first_size,
                    const std::uint64_t second_offset,
                    const std::uint64_t second_size) noexcept {
    if (first_size == 0U || second_size == 0U) return false;
    return first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

bool read_exact(std::ifstream& stream, const std::uint64_t offset,
                std::uint8_t* destination, const std::size_t size) {
    if (offset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max()) ||
        size > static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) return false;
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) return false;
    stream.read(reinterpret_cast<char*>(destination),
                static_cast<std::streamsize>(size));
    return stream.good() || stream.gcount() == static_cast<std::streamsize>(size);
}

} // namespace

bool inspect_nsp_file(const std::filesystem::path& path,
                      NspPackageInfo& output, std::string* error) {
    if (path.empty() || !equal_ascii_case_insensitive(path.string(), ".nsp")) {
        set_error(error, "An .nsp file path is required.");
        return false;
    }

    std::error_code file_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, file_error);
    if (file_error || file_size < Pfs0HeaderSize) {
        set_error(error, "The NSP file is missing or shorter than a PFS0 header.");
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        set_error(error, "Could not open the NSP file.");
        return false;
    }

    std::array<std::uint8_t, Pfs0HeaderSize> header{};
    if (!read_exact(input, 0U, header.data(), header.size())) {
        set_error(error, "Could not read the PFS0 header.");
        return false;
    }
    if (header[0] != 'P' || header[1] != 'F' ||
        header[2] != 'S' || header[3] != '0') {
        set_error(error, "The .nsp file does not start with a PFS0 container header.");
        return false;
    }

    const std::uint32_t file_count = read_u32(header.data() + 4U);
    const std::uint32_t string_table_size = read_u32(header.data() + 8U);
    const std::uint32_t reserved = read_u32(header.data() + 12U);
    if (reserved != 0U) {
        set_error(error, "The PFS0 header reserved field is nonzero.");
        return false;
    }
    if (file_count == 0U) {
        set_error(error, "The PFS0 directory contains no files.");
        return false;
    }
    if (file_count > MaximumNspEntryCount) {
        set_error(error, "The PFS0 file count exceeds the 8192-entry safety limit.");
        return false;
    }
    if (string_table_size > MaximumNspStringTableSize) {
        set_error(error, "The PFS0 string table exceeds the 16 MiB safety limit.");
        return false;
    }

    const std::uint64_t table_size =
        static_cast<std::uint64_t>(file_count) * Pfs0EntrySize;
    const std::uint64_t strings_offset =
        static_cast<std::uint64_t>(Pfs0HeaderSize) + table_size;
    const std::uint64_t data_offset =
        strings_offset + static_cast<std::uint64_t>(string_table_size);
    if (data_offset > static_cast<std::uint64_t>(file_size) ||
        data_offset > kMaximumMetadataSize) {
        set_error(error, "The PFS0 entry table or string table extends beyond the NSP file.");
        return false;
    }

    try {
        std::vector<std::uint8_t> metadata(static_cast<std::size_t>(
            table_size + string_table_size));
        if (!metadata.empty() &&
            !read_exact(input, Pfs0HeaderSize, metadata.data(), metadata.size())) {
            set_error(error, "Could not read the PFS0 entry table and string table.");
            return false;
        }

        const auto* strings = metadata.data() +
            static_cast<std::ptrdiff_t>(table_size);
        NspPackageInfo candidate{};
        candidate.package_size = static_cast<std::uint64_t>(file_size);
        candidate.pfs0_data_offset = data_offset;
        candidate.file_count = file_count;
        candidate.string_table_size = string_table_size;
        candidate.files.reserve(file_count);

        for (std::uint32_t index = 0U; index < file_count; ++index) {
            const std::size_t entry_offset =
                static_cast<std::size_t>(index) * Pfs0EntrySize;
            const std::uint8_t* entry = metadata.data() +
                static_cast<std::ptrdiff_t>(entry_offset);
            const std::uint64_t data_relative_offset = read_u64(entry);
            const std::uint64_t data_size = read_u64(entry + 8U);
            const std::uint32_t name_offset = read_u32(entry + 16U);
            const std::uint32_t entry_reserved = read_u32(entry + 20U);

            if (entry_reserved != 0U) {
                set_error(error, "A PFS0 file entry has a nonzero reserved field.");
                return false;
            }
            if (name_offset >= string_table_size) {
                set_error(error, "A PFS0 filename offset is outside the string table.");
                return false;
            }
            if (data_relative_offset >
                    static_cast<std::uint64_t>(file_size) - data_offset ||
                data_size > static_cast<std::uint64_t>(file_size) -
                    data_offset - data_relative_offset) {
                set_error(error, "A PFS0 file data range extends beyond the NSP file.");
                return false;
            }

            const std::size_t name_start = name_offset;
            const std::size_t string_size = string_table_size;
            std::size_t name_end = name_start;
            while (name_end < string_size && strings[name_end] != 0U) ++name_end;
            if (name_end == name_start || name_end == string_size ||
                name_end - name_start > 4096U) {
                set_error(error, "A PFS0 filename is empty, too long, or not NUL-terminated.");
                return false;
            }
            std::string name(reinterpret_cast<const char*>(strings + name_start),
                             name_end - name_start);
            if (name.find('\0') != std::string::npos) {
                set_error(error, "A PFS0 filename contains an invalid embedded NUL.");
                return false;
            }

            for (const auto& previous : candidate.files) {
                if (ranges_overlap(data_relative_offset, data_size,
                                   previous.offset, previous.size)) {
                    set_error(error, "PFS0 file data ranges overlap.");
                    return false;
                }
            }

            if (equal_ascii_case_insensitive(name, ".nca")) {
                ++candidate.nca_count;
                candidate.contains_nca_content = true;
                std::string lower_name = name;
                std::transform(lower_name.begin(), lower_name.end(),
                               lower_name.begin(), [](const unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });
                if (lower_name.find("cnmt") != std::string::npos) {
                    ++candidate.metadata_nca_count;
                }
            } else if (equal_ascii_case_insensitive(name, ".tik")) {
                ++candidate.ticket_count;
            } else if (equal_ascii_case_insensitive(name, ".cert")) {
                ++candidate.certificate_count;
            } else {
                ++candidate.other_file_count;
            }

            candidate.files.push_back(
                NspFileEntry{std::move(name), data_relative_offset, data_size});
        }

        candidate.launchable_by_current_engine = false;
        output = std::move(candidate);
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to inspect the NSP directory.");
        return false;
    }
    return true;
}

} // namespace lucid::switchfmt
