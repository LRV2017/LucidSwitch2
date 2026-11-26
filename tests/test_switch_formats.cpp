#include "lucid/switch/executable.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(const bool condition, const char* message) {
    if (condition) {
        std::cout << "[PASS] " << message << "\n";
    } else {
        ++failures;
        std::cerr << "[FAIL] " << message << "\n";
    }
}

void write_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void write_magic(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                 const std::array<std::uint8_t, 4U> magic) {
    for (std::size_t index = 0U; index < magic.size(); ++index)
        bytes[offset + index] = magic[index];
}

std::vector<std::uint8_t> make_nro() {
    std::vector<std::uint8_t> bytes(0xB0U, 0U);
    write_u32(bytes, 0x00U, 0U);
    write_u32(bytes, 0x04U, 0x80U);
    write_u32(bytes, 0x08U, 0U);
    write_magic(bytes, 0x10U, {'N', 'R', 'O', '0'});
    write_u32(bytes, 0x14U, 0U);
    write_u32(bytes, 0x18U, static_cast<std::uint32_t>(bytes.size()));
    write_u32(bytes, 0x1CU, 0U);
    write_u32(bytes, 0x20U, 0x80U);
    write_u32(bytes, 0x24U, 0x10U);
    write_u32(bytes, 0x28U, 0x90U);
    write_u32(bytes, 0x2CU, 0x10U);
    write_u32(bytes, 0x30U, 0xA0U);
    write_u32(bytes, 0x34U, 0x10U);
    write_u32(bytes, 0x38U, 0x80U);
    for (std::size_t index = 0U; index < 32U; ++index)
        bytes[0x40U + index] = static_cast<std::uint8_t>(index);
    return bytes;
}

std::vector<std::uint8_t> make_nso() {
    std::vector<std::uint8_t> bytes(0x130U, 0U);
    write_magic(bytes, 0U, {'N', 'S', 'O', '0'});
    write_u32(bytes, 0x04U, 0U);
    write_u32(bytes, 0x0CU, 0U);

    write_u32(bytes, 0x10U, 0x100U);
    write_u32(bytes, 0x14U, 0x0000U);
    write_u32(bytes, 0x18U, 0x10U);

    write_u32(bytes, 0x20U, 0x110U);
    write_u32(bytes, 0x24U, 0x1000U);
    write_u32(bytes, 0x28U, 0x10U);

    write_u32(bytes, 0x30U, 0x120U);
    write_u32(bytes, 0x34U, 0x2000U);
    write_u32(bytes, 0x38U, 0x10U);
    write_u32(bytes, 0x3CU, 0x100U);
    for (std::size_t index = 0U; index < 32U; ++index)
        bytes[0x40U + index] = static_cast<std::uint8_t>(0xA0U + index);
    return bytes;
}

void test_nro_metadata() {
    using namespace lucid::switchfmt;
    const auto bytes = make_nro();
    ExecutableInfo info{};
    std::string error;
    expect(inspect_executable(bytes, info, &error),
           "inspect a structurally valid plaintext NRO0 image");
    expect(info.format == ExecutableFormat::Nro0 &&
           info.version == 0U && info.declared_image_size == 0xB0U,
           "read NRO format identity and declared image size");
    expect(info.segments[0].kind == SegmentKind::Text &&
           info.segments[0].file_offset == 0x80U &&
           info.segments[1].file_offset == 0x90U &&
           info.segments[2].file_offset == 0xA0U,
           "read NRO text, rodata, and data ranges");
    expect(info.bss_size == 0x80U && info.build_id[31] == 31U,
           "read NRO BSS size and module build ID");
    expect(info.module_header_version == 0U &&
           info.module_header_location == 0x80U &&
           info.module_version_offset == 0U &&
           info.additional_header_offset == 0U,
           "read legacy NRO MOD0 pointer and versioned module-header fields");
    expect(!info.has_asset_section && info.trailing_bytes == 0U,
           "report the absence of an appended NRO ASET section");
}

void test_nro_assets_and_rejection() {
    using namespace lucid::switchfmt;
    auto bytes = make_nro();
    const std::uint32_t declared_size = static_cast<std::uint32_t>(bytes.size());
    bytes.resize(bytes.size() + 0x3CU, 0U);
    write_magic(bytes, declared_size, {'A', 'S', 'E', 'T'});
    write_u32(bytes, declared_size + 4U, 0U);
    write_u64(bytes, declared_size + 8U, 0x38U);
    write_u64(bytes, declared_size + 16U, 4U);
    bytes[declared_size + 0x38U] = 'I';
    bytes[declared_size + 0x39U] = 'C';
    bytes[declared_size + 0x3AU] = 'O';
    bytes[declared_size + 0x3BU] = 'N';

    ExecutableInfo info{};
    std::string error;
    expect(inspect_executable(bytes, info, &error) && info.has_asset_section,
           "recognize and bound the optional NRO ASET asset section");

    auto damaged = make_nro();
    write_u32(damaged, 0x28U, 0x88U);
    expect(!inspect_executable(damaged, info, &error),
           "reject overlapping NRO file segments");

    damaged = make_nro();
    write_u32(damaged, 0x1CU, 0x80000000U);
    expect(!inspect_executable(damaged, info, &error),
           "reject unknown NRO flags");

    damaged = make_nro();
    write_u32(damaged, 0x18U, 0xFFFFFFFFU);
    expect(!inspect_executable(damaged, info, &error),
           "reject NRO declared size outside the input file");

    damaged = make_nro();
    write_u32(damaged, 0x1CU, 0x2U);
    expect(!inspect_executable(damaged, info, &error),
           "reject compressed NRO variants without a compatible segment decoder");

    damaged = make_nro();
    write_u32(damaged, 0x3CU, 0xFFFFFFFFU);
    expect(!inspect_executable(damaged, info, &error),
           "reject an NRO additional-header offset outside the image");

    damaged = make_nro();
    write_u32(damaged, 0x04U, 0xFFFFFFFFU);
    expect(!inspect_executable(damaged, info, &error),
           "reject an NRO MOD0 pointer outside the image");

    damaged = make_nro();
    write_u32(damaged, 0x00U, 17U);
    write_u32(damaged, 0x08U, 0xFFFFFFFFU);
    expect(!inspect_executable(damaged, info, &error),
           "reject a versioned NRO module-version offset outside the image");

    damaged = make_nro();
    damaged.resize(0x30U);
    expect(!inspect_executable(damaged, info, &error),
           "reject a truncated NRO header");
}

void test_nso_metadata() {
    using namespace lucid::switchfmt;
    const auto bytes = make_nso();
    ExecutableInfo info{};
    std::string error;
    expect(inspect_executable(bytes, info, &error),
           "inspect a structurally valid plaintext NSO0 image");
    expect(info.format == ExecutableFormat::Nso0 &&
           info.segments[0].memory_offset == 0U &&
           info.segments[1].memory_offset == 0x1000U &&
           info.segments[2].memory_offset == 0x2000U,
           "read NSO file and guest memory offsets");
    expect(info.segments[0].stored_size == 0x10U &&
           info.segments[0].memory_size == 0x10U &&
           info.bss_size == 0x100U,
           "read NSO segment lengths and BSS reservation");
    expect(info.build_id[0] == 0xA0U && info.build_id[31] == 0xBFU,
           "read NSO module build ID");
    expect(info.declared_image_size == 0U && info.input_size == bytes.size(),
           "avoid inventing a declared-image-size field for NSO");
}

void test_nso_compression_metadata_and_rejection() {
    using namespace lucid::switchfmt;
    auto bytes = make_nso();
    write_u32(bytes, 0x0CU, (1U << 0U) | (1U << 3U) | (1U << 6U) | (1U << 7U));
    write_u32(bytes, 0x60U, 8U);

    ExecutableInfo info{};
    std::string error;
    expect(inspect_executable(bytes, info, &error),
           "inspect NSO files with declared compressed segment metadata");
    expect(info.segments[0].compressed && info.segments[0].hash_required &&
           info.uses_zstd_compression && info.segments[0].stored_size == 8U &&
           info.segments[0].execute_only,
           "report compression, hash-check, and Zstd flags without decoding data");

    auto damaged = make_nso();
    write_u32(damaged, 0x0CU, 0x80000000U);
    expect(!inspect_executable(damaged, info, &error),
           "reject unknown NSO flags");

    damaged = make_nso();
    write_u32(damaged, 0x04U, 1U);
    expect(!inspect_executable(damaged, info, &error),
           "reject an unsupported NSO version");

    damaged = make_nso();
    write_u32(damaged, 0x08U, 1U);
    expect(!inspect_executable(damaged, info, &error),
           "reject a nonzero reserved NSO header word");

    damaged = make_nso();
    write_u32(damaged, 0x10U, 0xFFFFFFF0U);
    expect(!inspect_executable(damaged, info, &error),
           "reject an NSO segment range outside the file");

    damaged = make_nso();
    write_u32(damaged, 0x24U, 0U);
    expect(!inspect_executable(damaged, info, &error),
           "reject overlapping NSO guest memory segments");

    damaged = make_nso();
    damaged.resize(0x80U);
    expect(!inspect_executable(damaged, info, &error),
           "reject a truncated NSO header");
}

void test_file_io_and_unknown_signature() {
    using namespace lucid::switchfmt;
    const auto path = std::filesystem::temp_directory_path() /
                      "lucidswitch2-switch-homebrew-test.nro";
    const auto bytes = make_nro();
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    ExecutableInfo info{};
    std::string error;
    expect(inspect_executable_file(path, info, &error) &&
           info.format == ExecutableFormat::Nro0,
           "inspect an NRO file from disk");
    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);

    const std::array<std::uint8_t, 12U> unknown{};
    expect(!inspect_executable(unknown, info, &error),
           "reject unknown executable formats without reading out of bounds");
    expect(to_string(ExecutableFormat::Nso0) == "NSO0" &&
           to_string(SegmentKind::ReadOnlyData) == ".rodata",
           "provide stable format and segment names");
}
} // namespace

int main() {
    test_nro_metadata();
    test_nro_assets_and_rejection();
    test_nso_metadata();
    test_nso_compression_metadata_and_rejection();
    test_file_io_and_unknown_signature();

    if (failures != 0) {
        std::cerr << failures << " Switch executable format test(s) failed.\n";
        return 1;
    }
    std::cout << "All Switch executable format tests passed.\n";
    return 0;
}
