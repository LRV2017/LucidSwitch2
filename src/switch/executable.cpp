#include "lucid/switch/executable.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>
#include <vector>

namespace lucid::switchfmt {
namespace {

constexpr std::uint32_t kNroKnownFlags = 0x1U;
constexpr std::uint32_t kNsoKnownFlags = 0xFFU;
constexpr std::uint32_t kNsoTextCompressed = 1U << 0U;
constexpr std::uint32_t kNsoRoCompressed = 1U << 1U;
constexpr std::uint32_t kNsoDataCompressed = 1U << 2U;
constexpr std::uint32_t kNsoTextHash = 1U << 3U;
constexpr std::uint32_t kNsoRoHash = 1U << 4U;
constexpr std::uint32_t kNsoDataHash = 1U << 5U;
constexpr std::uint32_t kNsoExecuteOnly = 1U << 6U;
constexpr std::uint32_t kNsoZstd = 1U << 7U;
constexpr std::uint64_t kNroAssetHeaderSize = 0x38ULL;

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

std::uint32_t read_u32(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) <<
                 (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) <<
                 (index * 8U);
    }
    return value;
}

bool has_magic(const std::span<const std::uint8_t> bytes,
               const std::size_t offset,
               const std::array<std::uint8_t, 4U>& magic) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < magic.size()) return false;
    return std::equal(magic.begin(), magic.end(),
                      bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

bool overlaps(const std::uint64_t first_offset, const std::uint64_t first_size,
              const std::uint64_t second_offset, const std::uint64_t second_size) noexcept {
    if (first_size == 0U || second_size == 0U) return false;
    return first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

bool validate_segment_ranges(const std::array<SegmentInfo, 3U>& segments,
                             const std::uint64_t file_limit,
                             const std::uint64_t memory_limit,
                             const std::uint64_t minimum_file_offset,
                             std::string* error) {
    std::uint64_t total_memory = 0U;
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        const auto& segment = segments[index];
        const std::uint64_t file_offset = segment.file_offset;
        const std::uint64_t file_size = segment.stored_size;
        const std::uint64_t memory_offset = segment.memory_offset;
        const std::uint64_t memory_size = segment.memory_size;

        if (file_size > MaximumSegmentSize || memory_size > MaximumSegmentSize) {
            set_error(error, "A program segment exceeds the 128 MiB safety limit.");
            return false;
        }
        if (file_size > 0U &&
            (file_offset < minimum_file_offset ||
             file_offset > file_limit ||
             file_size > file_limit - file_offset)) {
            set_error(error, "A program segment points outside the declared file image.");
            return false;
        }
        if (memory_size > 0U &&
            (memory_offset > memory_limit ||
             memory_size > memory_limit - memory_offset)) {
            set_error(error, "A program segment exceeds the bounded guest memory image.");
            return false;
        }
        total_memory += memory_size;
        if (total_memory > MaximumMemoryImageSize) {
            set_error(error, "The combined program segments exceed the 512 MiB memory limit.");
            return false;
        }

        for (std::size_t previous = 0U; previous < index; ++previous) {
            const auto& other = segments[previous];
            if (overlaps(file_offset, file_size, other.file_offset, other.stored_size)) {
                set_error(error, "Program segments overlap in the file image.");
                return false;
            }
            if (overlaps(memory_offset, memory_size,
                         other.memory_offset, other.memory_size)) {
                set_error(error, "Program segments overlap in the guest memory image.");
                return false;
            }
        }
    }
    return true;
}

bool inspect_nro(const std::span<const std::uint8_t> bytes,
                 ExecutableInfo& output, std::string* error) {
    if (bytes.size() < NroHeaderSize) {
        set_error(error, "The NRO file is shorter than its 0x80-byte start and header block.");
        return false;
    }
    if (!has_magic(bytes, 0x10U, {'N', 'R', 'O', '0'})) {
        set_error(error, "The file does not contain the NRO0 signature at offset 0x10.");
        return false;
    }

    ExecutableInfo candidate{};
    candidate.format = ExecutableFormat::Nro0;
    candidate.version = read_u32(bytes, 0x14U);
    candidate.declared_image_size = read_u32(bytes, 0x18U);
    candidate.flags = read_u32(bytes, 0x1CU);
    candidate.module_header_version = read_u32(bytes, 0x00U);
    candidate.module_header_location = read_u32(bytes, 0x04U);
    candidate.module_version_offset = read_u32(bytes, 0x08U);
    candidate.bss_size = read_u32(bytes, 0x38U);
    candidate.additional_header_offset = read_u32(bytes, 0x3CU);
    candidate.input_size = bytes.size();

    if (candidate.version != 0U) {
        set_error(error, "Unsupported NRO format version.");
        return false;
    }
    if ((candidate.flags & (1U << 1U)) != 0U) {
        set_error(error, "Compressed NRO variants are not supported by this classic file-offset inspector.");
        return false;
    }
    if ((candidate.flags & ~kNroKnownFlags) != 0U) {
        set_error(error, "The NRO header sets unknown or reserved flags.");
        return false;
    }
    if (candidate.declared_image_size < NroHeaderSize ||
        candidate.declared_image_size > bytes.size()) {
        set_error(error, "The NRO declared image size is outside the supplied file.");
        return false;
    }
    if (candidate.module_header_location != 0U &&
        candidate.module_header_location > candidate.declared_image_size - 4U) {
        set_error(error, "The NRO-start MOD0 header offset is outside the declared image.");
        return false;
    }
    if (candidate.module_header_version >= 17U &&
        candidate.module_version_offset != 0U &&
        candidate.module_version_offset > candidate.declared_image_size - 4U) {
        set_error(error, "The NRO module-version offset is outside the declared image.");
        return false;
    }
    if (candidate.additional_header_offset != 0U &&
        candidate.additional_header_offset > candidate.declared_image_size - 4U) {
        set_error(error, "The NRO additional-header offset is outside the declared image.");
        return false;
    }

    constexpr std::array<SegmentKind, 3U> kinds{
        SegmentKind::Text, SegmentKind::ReadOnlyData, SegmentKind::Data
    };
    for (std::size_t index = 0U; index < candidate.segments.size(); ++index) {
        const std::size_t offset = 0x20U + index * 8U;
        auto& segment = candidate.segments[index];
        segment.kind = kinds[index];
        segment.file_offset = read_u32(bytes, offset);
        segment.memory_offset = segment.file_offset;
        segment.stored_size = read_u32(bytes, offset + 4U);
        segment.memory_size = segment.stored_size;
    }

    std::copy_n(bytes.begin() + 0x40, candidate.build_id.size(),
                candidate.build_id.begin());

    if (candidate.bss_size > MaximumSegmentSize) {
        set_error(error, "The NRO BSS segment exceeds the 128 MiB safety limit.");
        return false;
    }
    if (!validate_segment_ranges(candidate.segments,
            candidate.declared_image_size, MaximumMemoryImageSize,
            NroHeaderSize, error)) return false;

    std::uint64_t image_end = 0U;
    for (const auto& segment : candidate.segments) {
        const std::uint64_t segment_end =
            static_cast<std::uint64_t>(segment.memory_offset) + segment.memory_size;
        image_end = std::max(image_end, segment_end);
    }
    if (image_end > MaximumMemoryImageSize ||
        candidate.bss_size > MaximumMemoryImageSize - image_end) {
        set_error(error, "The NRO segments and BSS exceed the bounded memory image.");
        return false;
    }

    candidate.trailing_bytes = bytes.size() - candidate.declared_image_size;
    if (candidate.trailing_bytes >= 4U &&
        has_magic(bytes, candidate.declared_image_size, {'A', 'S', 'E', 'T'})) {
        const std::uint64_t asset_base = candidate.declared_image_size;
        if (candidate.trailing_bytes < kNroAssetHeaderSize) {
            set_error(error, "The NRO ASET asset header is truncated.");
            return false;
        }
        if (read_u32(bytes, static_cast<std::size_t>(asset_base + 4U)) != 0U) {
            set_error(error, "The NRO ASET asset header version is unsupported.");
            return false;
        }
        for (std::size_t section = 0U; section < 3U; ++section) {
            const std::size_t offset = static_cast<std::size_t>(
                asset_base + 8U + section * 16U);
            const std::uint64_t section_offset = read_u64(bytes, offset);
            const std::uint64_t section_size = read_u64(bytes, offset + 8U);
            if (section_size == 0U) continue;
            if (section_offset < kNroAssetHeaderSize ||
                section_offset > candidate.trailing_bytes ||
                section_size > candidate.trailing_bytes - section_offset) {
                set_error(error, "An NRO ASET section points outside the supplied asset data.");
                return false;
            }
        }
        candidate.has_asset_section = true;
    }

    output = candidate;
    return true;
}

bool inspect_nso(const std::span<const std::uint8_t> bytes,
                 ExecutableInfo& output, std::string* error) {
    if (bytes.size() < NsoHeaderSize) {
        set_error(error, "The NSO file is shorter than its 0x100-byte header.");
        return false;
    }
    if (!has_magic(bytes, 0U, {'N', 'S', 'O', '0'})) {
        set_error(error, "The file does not contain the NSO0 signature.");
        return false;
    }

    ExecutableInfo candidate{};
    candidate.format = ExecutableFormat::Nso0;
    candidate.version = read_u32(bytes, 0x04U);
    candidate.flags = read_u32(bytes, 0x0CU);
    candidate.input_size = bytes.size();
    candidate.declared_image_size = 0U;
    candidate.bss_size = read_u32(bytes, 0x3CU);
    candidate.uses_zstd_compression = (candidate.flags & kNsoZstd) != 0U;
    std::copy_n(bytes.begin() + 0x40, candidate.build_id.size(),
                candidate.build_id.begin());

    if (candidate.version != 0U) {
        set_error(error, "Unsupported NSO format version.");
        return false;
    }
    if (read_u32(bytes, 0x08U) != 0U) {
        set_error(error, "The NSO reserved header word must be zero.");
        return false;
    }
    if ((candidate.flags & ~kNsoKnownFlags) != 0U) {
        set_error(error, "The NSO header sets unknown or reserved flags.");
        return false;
    }
    if (candidate.bss_size > MaximumSegmentSize) {
        set_error(error, "The NSO BSS segment exceeds the 128 MiB safety limit.");
        return false;
    }

    constexpr std::array<SegmentKind, 3U> kinds{
        SegmentKind::Text, SegmentKind::ReadOnlyData, SegmentKind::Data
    };
    constexpr std::array<std::uint32_t, 3U> compressed_flags{
        kNsoTextCompressed, kNsoRoCompressed, kNsoDataCompressed
    };
    constexpr std::array<std::uint32_t, 3U> hash_flags{
        kNsoTextHash, kNsoRoHash, kNsoDataHash
    };
    for (std::size_t index = 0U; index < candidate.segments.size(); ++index) {
        const std::size_t offset = 0x10U + index * 0x10U;
        const std::uint32_t file_offset = read_u32(bytes, offset);
        const std::uint32_t memory_offset = read_u32(bytes, offset + 4U);
        const std::uint32_t memory_size = read_u32(bytes, offset + 8U);
        const bool compressed = (candidate.flags & compressed_flags[index]) != 0U;
        const std::uint32_t stored_size = compressed
            ? read_u32(bytes, 0x60U + index * 4U)
            : memory_size;

        auto& segment = candidate.segments[index];
        segment.kind = kinds[index];
        segment.file_offset = file_offset;
        segment.memory_offset = memory_offset;
        segment.stored_size = stored_size;
        segment.memory_size = memory_size;
        segment.compressed = compressed;
        segment.hash_required = (candidate.flags & hash_flags[index]) != 0U;
        segment.execute_only =
            index == 0U && (candidate.flags & kNsoExecuteOnly) != 0U;
    }

    if (!validate_segment_ranges(candidate.segments, bytes.size(),
            MaximumMemoryImageSize, NsoHeaderSize, error)) return false;

    for (const auto& segment : candidate.segments) {
        if (segment.memory_size > 0U && segment.stored_size == 0U) {
            set_error(error, "An NSO segment has a nonzero memory size but no stored bytes.");
            return false;
        }
    }

    output = candidate;
    return true;
}

} // namespace

bool inspect_executable(const std::span<const std::uint8_t> bytes,
                        ExecutableInfo& output, std::string* error) {
    if (bytes.empty() || bytes.size() > MaximumInputSize) {
        set_error(error, "The executable file is empty or exceeds the 256 MiB inspection limit.");
        return false;
    }

    if (has_magic(bytes, 0U, {'N', 'S', 'O', '0'}))
        return inspect_nso(bytes, output, error);
    if (has_magic(bytes, 0x10U, {'N', 'R', 'O', '0'}))
        return inspect_nro(bytes, output, error);

    set_error(error, "Unsupported executable signature; expected plaintext NRO0 or NSO0.");
    return false;
}

bool inspect_executable_file(const std::filesystem::path& path,
                             ExecutableInfo& output, std::string* error) {
    if (path.empty()) {
        set_error(error, "An executable file path is required.");
        return false;
    }
    std::error_code file_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, file_error);
    if (file_error || file_size == 0U || file_size > MaximumInputSize) {
        set_error(error, "Could not determine a nonempty executable file size within the 256 MiB limit.");
        return false;
    }

    try {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            set_error(error, "Could not open the executable file for reading.");
            return false;
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            set_error(error, "The executable file could not be read completely.");
            return false;
        }
        return inspect_executable(bytes, output, error);
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to inspect the executable file.");
        return false;
    }
}

std::string_view to_string(const ExecutableFormat format) noexcept {
    switch (format) {
        case ExecutableFormat::Nro0: return "NRO0";
        case ExecutableFormat::Nso0: return "NSO0";
    }
    return "unknown executable format";
}

std::string_view to_string(const SegmentKind kind) noexcept {
    switch (kind) {
        case SegmentKind::Text: return ".text";
        case SegmentKind::ReadOnlyData: return ".rodata";
        case SegmentKind::Data: return ".data";
    }
    return "unknown segment";
}

} // namespace lucid::switchfmt
