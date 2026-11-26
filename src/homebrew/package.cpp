#include "lucid/homebrew/package.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

namespace lucid::homebrew {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic{
    'L', 'U', 'C', 'I', 'D', 'X', 'P', 0U
};
constexpr std::uint32_t kKnownFlags = 0U;
constexpr std::uint32_t kMaximumCodeSize = 8U * 1024U * 1024U;
constexpr std::uint32_t kMaximumDataSize = 8U * 1024U * 1024U;
constexpr std::uint32_t kMaximumBssSize = 16U * 1024U * 1024U;
constexpr std::uint32_t kMinimumStackSize = 4U * 1024U;
constexpr std::uint32_t kMaximumStackSize = 16U * 1024U * 1024U;
constexpr std::uint64_t kAddressLimit = 1ULL << 48U;

void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

void write_u16(std::span<std::uint8_t> bytes, const std::size_t offset,
               const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(std::span<std::uint8_t> bytes, const std::size_t offset,
               const std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::span<std::uint8_t> bytes, const std::size_t offset,
               const std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::uint8_t> bytes,
                                     const std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::uint8_t> bytes,
                                     const std::size_t offset) {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) <<
                 (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64(std::span<const std::uint8_t> bytes,
                                     const std::size_t offset) {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) <<
                 (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] bool validate_program(const ProgramPackage& program,
                                    std::string* error) {
    if (program.load_address == 0U ||
        (program.load_address & 0xFFFU) != 0U ||
        program.load_address >= kAddressLimit) {
        set_error(error, "The load address must be 4 KiB aligned and inside the lower 48-bit address range.");
        return false;
    }
    if (program.code.empty() ||
        program.code.size() > kMaximumCodeSize ||
        (program.code.size() & 3U) != 0U) {
        set_error(error, "Code must be nonempty, 4-byte aligned, and no larger than 8 MiB.");
        return false;
    }
    if (program.data.size() > kMaximumDataSize) {
        set_error(error, "The data segment exceeds the 8 MiB package limit.");
        return false;
    }
    if ((program.entry_offset & 3U) != 0U ||
        static_cast<std::uint64_t>(program.entry_offset) >=
            static_cast<std::uint64_t>(program.code.size())) {
        set_error(error, "The entry offset must be 4-byte aligned and inside the code segment.");
        return false;
    }
    if (program.bss_size > kMaximumBssSize) {
        set_error(error, "The zero-initialized BSS segment exceeds 16 MiB.");
        return false;
    }
    if (program.stack_size < kMinimumStackSize ||
        program.stack_size > kMaximumStackSize ||
        (program.stack_size & 0xFU) != 0U) {
        set_error(error, "The stack size must be 16-byte aligned and between 4 KiB and 16 MiB.");
        return false;
    }

    const std::uint64_t code_size = static_cast<std::uint64_t>(program.code.size());
    const std::uint64_t data_size = static_cast<std::uint64_t>(program.data.size());
    const std::uint64_t footprint = code_size + data_size +
        static_cast<std::uint64_t>(program.bss_size) +
        static_cast<std::uint64_t>(program.stack_size);
    if (footprint > MaximumGuestFootprint ||
        footprint > kAddressLimit - program.load_address) {
        set_error(error, "The program image and stack exceed the bounded 64 MiB guest footprint or address range.");
        return false;
    }
    return true;
}

} // namespace

bool encode_program(const ProgramPackage& program,
                    std::vector<std::uint8_t>& output,
                    std::string* error) {
    if (!validate_program(program, error)) return false;

    const std::size_t payload_size = program.code.size() + program.data.size();
    const std::size_t total_size = PackageHeaderSize + payload_size;
    if (total_size > MaximumPackageFileSize) {
        set_error(error, "The encoded package exceeds the 16 MiB file limit.");
        return false;
    }

    try {
        std::vector<std::uint8_t> encoded(total_size, 0U);
        std::copy(kMagic.begin(), kMagic.end(), encoded.begin());
        std::span<std::uint8_t> bytes(encoded);
        write_u16(bytes, 8U, PackageVersion);
        write_u16(bytes, 10U, static_cast<std::uint16_t>(PackageHeaderSize));
        write_u32(bytes, 12U, kKnownFlags);
        write_u64(bytes, 16U, program.load_address);
        write_u32(bytes, 24U, program.entry_offset);
        write_u32(bytes, 28U, static_cast<std::uint32_t>(program.code.size()));
        write_u32(bytes, 32U, static_cast<std::uint32_t>(program.data.size()));
        write_u32(bytes, 36U, program.bss_size);
        write_u32(bytes, 40U, program.stack_size);

        std::copy(program.code.begin(), program.code.end(),
                  encoded.begin() + static_cast<std::ptrdiff_t>(PackageHeaderSize));
        std::copy(program.data.begin(), program.data.end(),
                  encoded.begin() + static_cast<std::ptrdiff_t>(
                      PackageHeaderSize + program.code.size()));
        const std::span<const std::uint8_t> payload(
            encoded.data() + PackageHeaderSize, payload_size);
        write_u32(bytes, 44U, crc32(payload));
        output = std::move(encoded);
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to encode the package.");
        return false;
    }
    return true;
}

bool decode_program(const std::span<const std::uint8_t> bytes,
                    ProgramPackage& output,
                    std::string* error) {
    if (bytes.size() < PackageHeaderSize ||
        bytes.size() > MaximumPackageFileSize) {
        set_error(error, "The package size is outside the valid 48-byte to 16 MiB range.");
        return false;
    }
    for (std::size_t index = 0U; index < kMagic.size(); ++index) {
        if (bytes[index] != kMagic[index]) {
            set_error(error, "The package magic does not identify a Lucid LXP file.");
            return false;
        }
    }
    if (read_u16(bytes, 8U) != PackageVersion) {
        set_error(error, "Unsupported LXP package version.");
        return false;
    }
    if (read_u16(bytes, 10U) != PackageHeaderSize) {
        set_error(error, "The LXP header size is invalid.");
        return false;
    }
    if (read_u32(bytes, 12U) != kKnownFlags) {
        set_error(error, "The LXP package sets unknown or reserved flags.");
        return false;
    }

    const std::uint32_t code_size = read_u32(bytes, 28U);
    const std::uint32_t data_size = read_u32(bytes, 32U);
    const std::uint64_t payload_size =
        static_cast<std::uint64_t>(code_size) + data_size;
    if (payload_size != static_cast<std::uint64_t>(
            bytes.size() - PackageHeaderSize)) {
        set_error(error, "The LXP payload length does not match its header.");
        return false;
    }

    const std::span<const std::uint8_t> payload = bytes.subspan(PackageHeaderSize);
    if (crc32(payload) != read_u32(bytes, 44U)) {
        set_error(error, "The LXP payload CRC-32 check failed.");
        return false;
    }

    ProgramPackage candidate;
    candidate.load_address = read_u64(bytes, 16U);
    candidate.entry_offset = read_u32(bytes, 24U);
    candidate.bss_size = read_u32(bytes, 36U);
    candidate.stack_size = read_u32(bytes, 40U);
    try {
        const std::size_t code_length = static_cast<std::size_t>(code_size);
        const std::size_t data_length = static_cast<std::size_t>(data_size);
        candidate.code.assign(payload.begin(),
                              payload.begin() + static_cast<std::ptrdiff_t>(code_length));
        candidate.data.assign(payload.begin() + static_cast<std::ptrdiff_t>(code_length),
                              payload.begin() + static_cast<std::ptrdiff_t>(
                                  code_length + data_length));
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to decode the package.");
        return false;
    }

    if (!validate_program(candidate, error)) return false;
    output = std::move(candidate);
    return true;
}

bool read_program_file(const std::filesystem::path& path,
                       ProgramPackage& output,
                       std::string* error) {
    if (path.empty()) {
        set_error(error, "A package file path is required.");
        return false;
    }
    std::error_code file_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        set_error(error, "Could not determine the package file size.");
        return false;
    }
    if (file_size < PackageHeaderSize || file_size > MaximumPackageFileSize) {
        set_error(error, "The package file size is outside the 48-byte to 16 MiB limit.");
        return false;
    }
    try {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            set_error(error, "Could not open the package file for reading.");
            return false;
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            set_error(error, "The package file could not be read completely.");
            return false;
        }
        return decode_program(bytes, output, error);
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to read the package file.");
        return false;
    }
}

bool write_program_file(const std::filesystem::path& path,
                        const ProgramPackage& program,
                        std::string* error) {
    if (path.empty()) {
        set_error(error, "A package file path is required.");
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!encode_program(program, bytes, error)) return false;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        set_error(error, "Could not open the destination package file for writing.");
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        set_error(error, "The package file could not be written completely.");
        return false;
    }
    return true;
}

} // namespace lucid::homebrew
