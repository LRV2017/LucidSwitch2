#include "lucid/switch/homebrew_loader.hpp"

#include "lucid/switch/executable.hpp"

#include <algorithm>
#include <array>
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

std::uint32_t read_u32(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) <<
                 (index * 8U);
    }
    return value;
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

bool get_entry_offset(const std::span<const std::uint8_t> bytes,
                      const ExecutableInfo& info,
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
    // the supported minimal, relocation-free test profile.
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
    if (info.module_header_location != 0U) {
        set_error(error, "This minimal runner does not process MOD0 dynamic metadata or relocations.");
        return false;
    }

    std::uint64_t entry_offset = 0U;
    if (!get_entry_offset(bytes, info, entry_offset, error)) return false;

    std::uint64_t guest_size = 0U;
    if (!calculate_guest_size(info, guest_size, error)) return false;

    core::Emulator emulator;
    core::EmulatorConfig config{};
    config.memory_base = kGuestImageBase;
    config.memory_size = static_cast<std::size_t>(guest_size);
    config.entry_point = kGuestImageBase;
    if (!emulator.initialize(config, error)) return false;

    const auto image = std::span<const std::uint8_t>(
        bytes.data(), static_cast<std::size_t>(info.declared_image_size));
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
    report.program_counter = context.program_counter;
    report.run_result = run_result;
    report.last_step = emulator.last_step_result();
    return true;
}

} // namespace lucid::switchfmt
