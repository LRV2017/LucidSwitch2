#include "lucid/switch/homebrew_loader.hpp"

#include "lucid/switch/executable.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <vector>

namespace lucid::switchfmt {
namespace {

constexpr std::uint64_t kGuestImageBase = 0x10000000ULL;
constexpr std::uint64_t kGuestPageSize = 0x1000ULL;

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

std::int64_t sign_extend_26(const std::uint32_t value) noexcept {
    const std::uint32_t immediate = value & 0x03FFFFFFU;
    if ((immediate & 0x02000000U) != 0U) {
        return static_cast<std::int64_t>(immediate) -
               static_cast<std::int64_t>(0x04000000U);
    }
    return static_cast<std::int64_t>(immediate);
}

bool load_file(const std::filesystem::path& path,
               std::vector<std::uint8_t>& bytes,
               std::string* error) {
    if (path.empty()) {
        set_error(error, "An NRO file path is required.");
        return false;
    }
    std::error_code file_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, file_error);
    if (file_error || file_size == 0U || file_size > MaximumInputSize) {
        set_error(error, "The NRO file must be nonempty and no larger than 256 MiB.");
        return false;
    }

    try {
        bytes.resize(static_cast<std::size_t>(file_size));
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            set_error(error, "Could not open the NRO file.");
            return false;
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            set_error(error, "The NRO file could not be read completely.");
            return false;
        }
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to read the NRO file.");
        return false;
    }
    return true;
}

bool get_entry_offset(const ExecutableInfo& info,
                      std::uint64_t& entry_offset,
                      std::string* error) {
    if (info.format != ExecutableFormat::Nro0) {
        set_error(error, "The restricted homebrew runner currently accepts NRO0 only.");
        return false;
    }
    if (info.flags != 0U) {
        set_error(error, "NRO flag variants are outside the restricted runner profile.");
        return false;
    }
    if (info.segments[0].memory_size == 0U ||
        info.segments[0].stored_size == 0U) {
        set_error(error, "The NRO has no nonempty text segment.");
        return false;
    }

    // The Homebrew ABI defines binary_ptr + 0 as the entry stub. It branches
    // past the NRO0 header into .text. Only direct B-immediate stubs are in
    // the supported minimal entry profile; the MOD0 path below accepts only
    // symbol-free relative relocations.
    const std::uint32_t branch = info.entry_stub_instruction;
    if ((branch & 0xFC000000U) != 0x14000000U) {
        set_error(error, "The NRO entry stub is not a direct A64 B instruction.");
        return false;
    }
    const std::int64_t displacement = sign_extend_26(branch) * 4;
    if (displacement < 0) {
        set_error(error, "A backward NRO entry branch is outside the supported minimal profile.");
        return false;
    }

    entry_offset = static_cast<std::uint64_t>(displacement);
    const auto& text = info.segments[0];
    const std::uint64_t text_start = text.memory_offset;
    const std::uint64_t text_end = text_start + text.memory_size;
    if ((entry_offset & 3U) != 0U ||
        entry_offset < text_start || entry_offset >= text_end) {
        set_error(error, "The NRO entry branch does not land inside the text segment.");
        return false;
    }
    if (entry_offset > info.declared_image_size ||
        info.declared_image_size - entry_offset < 4U) {
        set_error(error, "The NRO entry branch points outside the declared image.");
        return false;
    }
    return true;
}

bool calculate_guest_size(const ExecutableInfo& info,
                          std::uint64_t& guest_size,
                          std::string* error) {
    std::uint64_t image_end = info.declared_image_size;
    for (const auto& segment : info.segments) {
        const std::uint64_t end =
            static_cast<std::uint64_t>(segment.memory_offset) +
            segment.memory_size;
        image_end = std::max(image_end, end);
    }
    if (image_end > MaximumMemoryImageSize ||
        info.bss_size > MaximumMemoryImageSize - image_end) {
        set_error(error, "NRO image and BSS exceed the 512 MiB guest memory limit.");
        return false;
    }
    std::uint64_t required = image_end + info.bss_size;
    if (NroHomebrewStackSize > MaximumMemoryImageSize - required) {
        set_error(error, "NRO stack reservation exceeds the 512 MiB guest memory limit.");
        return false;
    }
    required += NroHomebrewStackSize;
    if (required > std::numeric_limits<std::uint64_t>::max() -
                   (kGuestPageSize - 1U)) {
        set_error(error, "The NRO guest memory size overflows.");
        return false;
    }
    guest_size = (required + kGuestPageSize - 1U) &
                 ~(kGuestPageSize - 1U);
    if (guest_size > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        set_error(error, "The NRO guest memory image is too large for this host.");
        return false;
    }
    return true;
}

std::uint32_t read_image_u32(const std::vector<std::uint8_t>& bytes,
                             const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}
std::uint64_t read_image_u64(const std::vector<std::uint8_t>& bytes,
                             const std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i)
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8U);
    return value;
}
void write_image_u64(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                     const std::uint64_t value) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
}
bool resolve_signed_image_offset(const std::uint64_t base,
                                 const std::int32_t displacement,
                                 const std::uint64_t limit,
                                 std::uint64_t& result) noexcept {
    if (displacement < 0) {
        const std::uint64_t magnitude =
            static_cast<std::uint64_t>(-static_cast<std::int64_t>(displacement));
        if (magnitude > base) return false;
        result = base - magnitude;
    } else {
        const std::uint64_t amount = static_cast<std::uint32_t>(displacement);
        if (base > limit || amount > limit - base) return false;
        result = base + amount;
    }
    return result <= limit;
}
bool relocate_image_pointer(const std::int64_t addend,
                            const std::uint64_t image_size,
                            std::uint64_t& result) noexcept {
    if (addend < 0) {
        const std::uint64_t magnitude =
            static_cast<std::uint64_t>(-(addend + 1)) + 1U;
        if (magnitude > kGuestImageBase) return false;
        result = kGuestImageBase - magnitude;
    } else {
        const std::uint64_t amount = static_cast<std::uint64_t>(addend);
        if (amount > std::numeric_limits<std::uint64_t>::max() - kGuestImageBase)
            return false;
        result = kGuestImageBase + amount;
    }
    return result >= kGuestImageBase &&
           result - kGuestImageBase < image_size;
}
bool relocate_implicit_image_pointer(const std::uint64_t addend,
                                     const std::uint64_t image_size,
                                     std::uint64_t& result) noexcept {
    if (addend >= image_size ||
        addend > std::numeric_limits<std::uint64_t>::max() - kGuestImageBase)
        return false;
    result = kGuestImageBase + addend;
    return true;
}

bool prepare_nro_memory_image(const ExecutableInfo& info,
                              const std::vector<std::uint8_t>& file,
                              std::vector<std::uint8_t>& image,
                              std::uint64_t& relocation_count,
                              std::string* error) {
    std::uint64_t image_end = info.declared_image_size;
    for (const auto& segment : info.segments) {
        const std::uint64_t end =
            static_cast<std::uint64_t>(segment.memory_offset) + segment.memory_size;
        image_end = std::max(image_end, end);
    }
    if (image_end > MaximumMemoryImageSize ||
        info.bss_size > MaximumMemoryImageSize - image_end) {
        set_error(error, "The NRO image and BSS exceed the 512 MiB loader limit.");
        return false;
    }
    image_end += info.bss_size;
    try {
        image.assign(static_cast<std::size_t>(image_end), 0U);
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to allocate the NRO image and BSS.");
        return false;
    }
    std::copy_n(file.begin(), static_cast<std::size_t>(info.declared_image_size),
                image.begin());
    relocation_count = 0U;
    if (info.module_header_location == 0U) return true;

    const std::uint64_t mod0 = info.module_header_location;
    constexpr std::uint64_t Mod0HeaderSize = 0x18U;
    if (mod0 > info.declared_image_size ||
        Mod0HeaderSize > info.declared_image_size - mod0 ||
        read_image_u32(image, static_cast<std::size_t>(mod0)) != 0x30444F4DU) {
        set_error(error, "The NRO MOD0 pointer does not reference a complete MOD0 header.");
        return false;
    }
    const auto read_relative = [&](const std::size_t field,
                                   const std::uint64_t limit,
                                   std::uint64_t& value) {
        const auto raw = read_image_u32(image, static_cast<std::size_t>(mod0) + field);
        return resolve_signed_image_offset(
            mod0, std::bit_cast<std::int32_t>(raw), limit, value);
    };

    std::uint64_t bss_start = 0U, bss_end = 0U;
    if (!read_relative(8U, image_end, bss_start) ||
        !read_relative(12U, image_end, bss_end) ||
        bss_end < bss_start || bss_end - bss_start != info.bss_size ||
        bss_start < info.declared_image_size) {
        set_error(error, "The MOD0 BSS range is invalid or does not match the NRO BSS size.");
        return false;
    }

    std::uint64_t dynamic_offset = 0U;
    if (!read_relative(4U, info.declared_image_size, dynamic_offset) ||
        dynamic_offset > info.declared_image_size ||
        info.declared_image_size - dynamic_offset < 16U) {
        set_error(error, "The MOD0 dynamic-table offset is outside the NRO image.");
        return false;
    }

    std::uint64_t rela_offset = 0U, rela_size = 0U;
    std::uint64_t relr_offset = 0U, relr_size = 0U;
    std::uint64_t rela_entry_size = 24U, relr_entry_size = 8U;
    bool terminated = false;
    const std::uint64_t max_entries = std::min<std::uint64_t>(
        4096U, (info.declared_image_size - dynamic_offset) / 16U);
    for (std::uint64_t i = 0U; i < max_entries; ++i) {
        const std::size_t at = static_cast<std::size_t>(dynamic_offset + i * 16U);
        const std::int64_t tag =
            std::bit_cast<std::int64_t>(read_image_u64(image, at));
        const std::uint64_t value = read_image_u64(image, at + 8U);
        if (tag == 0) { terminated = true; break; }
        switch (tag) {
        case 7: rela_offset = value; break;       // DT_RELA
        case 8: rela_size = value; break;         // DT_RELASZ
        case 9: rela_entry_size = value; break;   // DT_RELAENT
        case 35: relr_size = value; break;        // DT_RELRSZ
        case 36: relr_offset = value; break;      // DT_RELR
        case 37: relr_entry_size = value; break;  // DT_RELRENT
        default: break;
        }
    }
    if (!terminated) {
        set_error(error, "The MOD0 dynamic table has no DT_NULL terminator within the bounded scan.");
        return false;
    }
    const auto table_valid = [&](const std::uint64_t offset,
                                 const std::uint64_t size,
                                 const std::uint64_t entry_size) {
        if (size == 0U) return offset == 0U || offset <= info.declared_image_size;
        return offset <= info.declared_image_size &&
               entry_size != 0U && size % entry_size == 0U &&
               size <= info.declared_image_size - offset;
    };
    if (!table_valid(rela_offset, rela_size, rela_entry_size) ||
        !table_valid(relr_offset, relr_size, relr_entry_size) ||
        (rela_size != 0U && rela_entry_size != 24U) ||
        (relr_size != 0U && relr_entry_size != 8U)) {
        set_error(error, "A MOD0 relocation table has an invalid or unsupported size/range.");
        return false;
    }

    for (std::uint64_t cursor = 0U; cursor < rela_size; cursor += 24U) {
        const std::size_t at = static_cast<std::size_t>(rela_offset + cursor);
        const std::uint64_t target = read_image_u64(image, at);
        const std::uint64_t info_word = read_image_u64(image, at + 8U);
        const std::int64_t addend =
            std::bit_cast<std::int64_t>(read_image_u64(image, at + 16U));
        const std::uint32_t type = static_cast<std::uint32_t>(info_word);
        const std::uint32_t symbol = static_cast<std::uint32_t>(info_word >> 32U);
        if (type == 0U) continue;
        if (type != 1027U || symbol != 0U ||
            target > image.size() || image.size() - target < 8U ||
            (target & 7U) != 0U) {
            set_error(error, "The NRO contains an unsupported or out-of-range RELA relocation.");
            return false;
        }
        std::uint64_t relocated = 0U;
        if (!relocate_image_pointer(addend, image.size(), relocated)) {
            set_error(error, "A RELATIVE relocation addend points outside the NRO image.");
            return false;
        }
        write_image_u64(image, static_cast<std::size_t>(target), relocated);
        ++relocation_count;
    }

    std::uint64_t next_target = 0U;
    bool have_next_target = false;
    for (std::uint64_t cursor = 0U; cursor < relr_size; cursor += 8U) {
        const std::uint64_t entry =
            read_image_u64(image, static_cast<std::size_t>(relr_offset + cursor));
        if ((entry & 1U) == 0U) {
            if (entry > image.size() || image.size() - entry < 8U ||
                (entry & 7U) != 0U) {
                set_error(error, "A RELR direct relocation target is outside the NRO image.");
                return false;
            }
            std::uint64_t relocated = 0U;
            if (!relocate_implicit_image_pointer(
                    read_image_u64(image, static_cast<std::size_t>(entry)),
                    image.size(), relocated)) {
                set_error(error, "A RELR addend points outside the NRO image.");
                return false;
            }
            write_image_u64(image, static_cast<std::size_t>(entry), relocated);
            ++relocation_count;
            next_target = entry + 8U;
            have_next_target = true;
        } else {
            if (!have_next_target) {
                set_error(error, "A RELR bitmap appears before a direct relocation target.");
                return false;
            }
            const std::uint64_t bitmap = entry >> 1U;
            for (unsigned bit = 0U; bit < 63U; ++bit) {
                if ((bitmap & (std::uint64_t{1} << bit)) == 0U) continue;
                const std::uint64_t target =
                    next_target + static_cast<std::uint64_t>(bit) * 8U;
                if (target > image.size() || image.size() - target < 8U) {
                    set_error(error, "A RELR bitmap target is outside the NRO image.");
                    return false;
                }
                std::uint64_t relocated = 0U;
                if (!relocate_implicit_image_pointer(
                        read_image_u64(image, static_cast<std::size_t>(target)),
                        image.size(), relocated)) {
                    set_error(error, "A RELR bitmap addend points outside the NRO image.");
                    return false;
                }
                write_image_u64(image, static_cast<std::size_t>(target), relocated);
                ++relocation_count;
            }
            if (next_target > std::numeric_limits<std::uint64_t>::max() - 63U * 8U) {
                set_error(error, "The RELR bitmap cursor overflows.");
                return false;
            }
            next_target += 63U * 8U;
        }
    }
    return true;
}


} // namespace

bool run_restricted_nro_homebrew(
    const std::filesystem::path& path,
    const std::uint64_t instruction_budget,
    NroHomebrewRunReport& report,
    std::string* error) {
    if (instruction_budget == 0U ||
        instruction_budget > MaximumNroInstructionBudget) {
        set_error(error, "The instruction budget must be between 1 and 10,000,000.");
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!load_file(path, bytes, error)) return false;

    ExecutableInfo info{};
    if (!inspect_executable(bytes, info, error)) return false;
    if (info.format != ExecutableFormat::Nro0) {
        set_error(error, "The restricted homebrew runner accepts NRO0, not NSO0.");
        return false;
    }
    if (info.declared_image_size > bytes.size()) {
        set_error(error, "The NRO declared image extends beyond the supplied file.");
        return false;
    }
    std::uint64_t entry_offset = 0U;
    if (!get_entry_offset(info, entry_offset, error)) return false;

    std::uint64_t guest_size = 0U;
    if (!calculate_guest_size(info, guest_size, error)) return false;

    std::vector<std::uint8_t> image;
    std::uint64_t relocation_count = 0U;
    if (!prepare_nro_memory_image(info, bytes, image, relocation_count, error))
        return false;

    core::Emulator emulator;
    core::EmulatorConfig config{};
    config.memory_base = kGuestImageBase;
    config.memory_size = static_cast<std::size_t>(guest_size);
    config.entry_point = kGuestImageBase;
    if (!emulator.initialize(config, error)) return false;

    if (!emulator.load_program(kGuestImageBase, image, error)) return false;

    // Minimal Homebrew ABI register setup. No loader configuration entries or
    // Horizon handles are available in this restricted research runtime.
    auto& context = emulator.cpu_context();
    context.x[0] = 0U;
    context.x[1] = std::numeric_limits<std::uint64_t>::max();
    context.sp = (kGuestImageBase + guest_size) & ~0xFULL;

    const core::RunResult run_result = emulator.run(instruction_budget);
    report.image_base = kGuestImageBase;
    report.entry_point = kGuestImageBase + entry_offset;
    report.stack_pointer = context.sp;
    report.instruction_budget = instruction_budget;
    report.instructions_retired = context.instructions_retired;
    report.register_x0 = context.x[0];
    report.relocations_applied = relocation_count;
    report.program_counter = context.program_counter;
    report.run_result = run_result;
    report.last_step = emulator.last_step_result();
    return true;
}

} // namespace lucid::switchfmt
