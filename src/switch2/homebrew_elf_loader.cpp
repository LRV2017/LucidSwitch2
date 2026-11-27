#include "lucid/switch2/homebrew_elf_loader.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace lucid::switch2 {
namespace {
constexpr std::size_t ElfHeaderSize = 64U;
constexpr std::size_t ProgramHeaderSize = 56U;
constexpr std::uint32_t PtLoad = 1U;
constexpr std::uint32_t PfExecute = 1U;
constexpr std::uint32_t PfWrite = 2U;
constexpr std::uint32_t PfRead = 4U;
constexpr std::uint16_t EtExecutable = 2U;
constexpr std::uint16_t EmAArch64 = 183U;

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

bool range_fits(const std::uint64_t offset, const std::uint64_t size,
                const std::size_t bound) noexcept {
    return offset <= bound && size <= static_cast<std::uint64_t>(bound) - offset;
}

std::uint16_t read_u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U);
}
std::uint32_t read_u32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8U) |
        (static_cast<std::uint32_t>(p[2]) << 16U) |
        (static_cast<std::uint32_t>(p[3]) << 24U);
}
std::uint64_t read_u64(const std::uint8_t* p) noexcept {
    return static_cast<std::uint64_t>(read_u32(p)) |
        (static_cast<std::uint64_t>(read_u32(p + 4U)) << 32U);
}

struct Segment {
    std::uint64_t offset{};
    std::uint64_t address{};
    std::uint64_t file_size{};
    std::uint64_t memory_size{};
    std::uint64_t alignment{};
    std::uint32_t flags{};
};

core::MemoryPermission to_permissions(const std::uint32_t flags) noexcept {
    auto permissions = core::MemoryPermission::None;
    if ((flags & PfRead) != 0U)
        permissions = permissions | core::MemoryPermission::Read;
    if ((flags & PfWrite) != 0U)
        permissions = permissions | core::MemoryPermission::Write;
    if ((flags & PfExecute) != 0U)
        permissions = permissions | core::MemoryPermission::Execute;
    return permissions;
}

bool overlaps(const Segment& left, const Segment& right) noexcept {
    return left.address < right.address + right.memory_size &&
           right.address < left.address + left.memory_size;
}
}

bool HomebrewElfLoader::load(const std::span<const std::uint8_t> image,
                             core::MemoryBus& memory,
                             HomebrewElfLoadInfo& info,
                             std::string* error) const {
    info = {};
    if (image.size() < ElfHeaderSize || image[0] != 0x7FU ||
        image[1] != 'E' || image[2] != 'L' || image[3] != 'F') {
        set_error(error, "Input is not an ELF image with a complete 64-byte header.");
        return false;
    }
    if (image[4] != 2U || image[5] != 1U || image[6] != 1U) {
        set_error(error, "Only ELF64 little-endian images with ELF version 1 are supported.");
        return false;
    }
    const auto type = read_u16(image.data() + 16U);
    const auto machine = read_u16(image.data() + 18U);
    const auto version = read_u32(image.data() + 20U);
    const auto entry = read_u64(image.data() + 24U);
    const auto phoff = read_u64(image.data() + 32U);
    const auto ehsize = read_u16(image.data() + 52U);
    const auto phentsize = read_u16(image.data() + 54U);
    const auto phnum = read_u16(image.data() + 56U);
    if (type != EtExecutable || machine != EmAArch64 || version != 1U ||
        ehsize != ElfHeaderSize || phentsize != ProgramHeaderSize ||
        phnum == 0U || phnum > MaxProgramHeaders) {
        set_error(error, "ELF must be a static ET_EXEC image for AArch64 with supported header sizes.");
        return false;
    }
    const std::uint64_t table_size =
        static_cast<std::uint64_t>(phentsize) * phnum;
    if (!range_fits(phoff, table_size, image.size())) {
        set_error(error, "ELF program-header table lies outside the input image.");
        return false;
    }

    std::vector<Segment> segments;
    segments.reserve(phnum);
    std::uint64_t total_memory = 0U;
    bool entry_is_executable = false;
    for (std::size_t index = 0U; index < phnum; ++index) {
        const std::size_t header_offset = static_cast<std::size_t>(phoff) +
            index * static_cast<std::size_t>(phentsize);
        const auto* p = image.data() + header_offset;
        if (read_u32(p) != PtLoad) continue;
        Segment segment{};
        segment.flags = read_u32(p + 4U);
        segment.offset = read_u64(p + 8U);
        segment.address = read_u64(p + 16U);
        segment.file_size = read_u64(p + 32U);
        segment.memory_size = read_u64(p + 40U);
        segment.alignment = read_u64(p + 48U);

        if ((segment.flags & ~0x7U) != 0U ||
            ((segment.flags & (PfWrite | PfExecute)) == (PfWrite | PfExecute))) {
            set_error(error, "ELF segment uses unsupported flags or requests writable executable memory.");
            return false;
        }
        if (segment.memory_size == 0U ||
            segment.file_size > segment.memory_size ||
            segment.memory_size > MaxSegmentBytes ||
            !range_fits(segment.offset, segment.file_size, image.size()) ||
            segment.address >= MaxGuestVirtualAddress ||
            segment.memory_size > MaxGuestVirtualAddress - segment.address) {
            set_error(error, "ELF PT_LOAD segment has invalid sizes or address ranges.");
            return false;
        }
        if (segment.alignment > 1U &&
            ((segment.alignment & (segment.alignment - 1U)) != 0U ||
             segment.alignment > (2U * 1024U * 1024U) ||
             (segment.offset % segment.alignment) !=
                (segment.address % segment.alignment))) {
            set_error(error, "ELF PT_LOAD alignment is invalid or incongruent.");
            return false;
        }
        if ((segment.flags & (PfRead | PfWrite | PfExecute)) == 0U) {
            set_error(error, "ELF PT_LOAD segment grants no usable permissions.");
            return false;
        }
        if (total_memory > MaxImageMemoryBytes - segment.memory_size) {
            set_error(error, "ELF image exceeds the configured guest-memory budget.");
            return false;
        }
        total_memory += segment.memory_size;
        if ((segment.flags & PfExecute) != 0U && entry >= segment.address &&
            entry - segment.address < segment.memory_size)
            entry_is_executable = true;
        for (const Segment& existing : segments) {
            if (overlaps(segment, existing)) {
                set_error(error, "ELF PT_LOAD segments overlap in guest virtual memory.");
                return false;
            }
        }
        segments.push_back(segment);
    }
    if (segments.empty() || !entry_is_executable || (entry & 3U) != 0U) {
        set_error(error, "ELF entry point is not aligned within an executable PT_LOAD segment.");
        return false;
    }

    std::vector<Segment> mapped;
    mapped.reserve(segments.size());
    for (const Segment& segment : segments) {
        if (!memory.map_region(segment.address,
                static_cast<std::size_t>(segment.memory_size), error,
                core::MemoryPermission::ReadWriteExecute)) {
            for (const Segment& prior : mapped)
                (void)memory.unmap_region(prior.address,
                    static_cast<std::size_t>(prior.memory_size));
            return false;
        }
        mapped.push_back(segment);
        if (segment.file_size != 0U) {
            const auto bytes = image.subspan(static_cast<std::size_t>(segment.offset),
                static_cast<std::size_t>(segment.file_size));
            if (!memory.write(segment.address, bytes, error)) {
                for (const Segment& prior : mapped)
                    (void)memory.unmap_region(prior.address,
                        static_cast<std::size_t>(prior.memory_size));
                return false;
            }
        }
        if (!memory.protect_region(segment.address,
                static_cast<std::size_t>(segment.memory_size),
                to_permissions(segment.flags), error)) {
            for (const Segment& prior : mapped)
                (void)memory.unmap_region(prior.address,
                    static_cast<std::size_t>(prior.memory_size));
            return false;
        }
    }
    info.entry_point = entry;
    info.loaded_segments = segments.size();
    info.total_memory_bytes = total_memory;
    return true;
}

} // namespace lucid::switch2
