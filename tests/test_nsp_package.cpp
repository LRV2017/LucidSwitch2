#include "lucid/switch/nsp_package.hpp"

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
    if (condition) std::cout << "[PASS] " << message << "\n";
    else {
        ++failures;
        std::cerr << "[FAIL] " << message << "\n";
    }
}

void write_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint32_t value) {
    for (std::size_t i = 0U; i < 4U; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
}

void write_u64(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint64_t value) {
    for (std::size_t i = 0U; i < 8U; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
}

void write_entry(std::vector<std::uint8_t>& bytes, const std::size_t index,
                 const std::uint64_t offset, const std::uint64_t size,
                 const std::uint32_t name_offset) {
    const std::size_t at = 0x10U + index * 0x18U;
    write_u64(bytes, at, offset);
    write_u64(bytes, at + 8U, size);
    write_u32(bytes, at + 16U, name_offset);
    write_u32(bytes, at + 20U, 0U);
}

std::vector<std::uint8_t> make_nsp() {
    const std::array<std::string, 4U> names{
        "00000000000000000000000000000001.nca",
        "00000000000000000000000000000002.cnmt.nca",
        "00000000000000000000000000000001.tik",
        "00000000000000000000000000000001.cert"
    };
    std::vector<std::uint8_t> strings;
    std::array<std::uint32_t, names.size()> offsets{};
    for (std::size_t i = 0U; i < names.size(); ++i) {
        offsets[i] = static_cast<std::uint32_t>(strings.size());
        for (const char ch : names[i])
            strings.push_back(static_cast<std::uint8_t>(ch));
        strings.push_back(0U);
    }
    constexpr std::array<std::uint64_t, 4U> sizes{64U, 48U, 16U, 8U};
    const std::uint64_t data_start = 0x10U + names.size() * 0x18U + strings.size();
    std::uint64_t total_data = 0U;
    for (const auto size : sizes) total_data += size;
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(data_start + total_data), 0U);
    bytes[0] = 'P'; bytes[1] = 'F'; bytes[2] = 'S'; bytes[3] = '0';
    write_u32(bytes, 4U, static_cast<std::uint32_t>(names.size()));
    write_u32(bytes, 8U, static_cast<std::uint32_t>(strings.size()));
    write_u32(bytes, 12U, 0U);
    std::uint64_t offset = 0U;
    for (std::size_t i = 0U; i < names.size(); ++i) {
        write_entry(bytes, i, offset, sizes[i], offsets[i]);
        offset += sizes[i];
    }
    const std::size_t string_at = 0x10U + names.size() * 0x18U;
    for (std::size_t i = 0U; i < strings.size(); ++i)
        bytes[string_at + i] = strings[i];
    return bytes;
}

bool write_file(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

void test_valid_nsp_directory() {
    using namespace lucid::switchfmt;
    const auto path = std::filesystem::temp_directory_path() /
                      "lucidswitch2-package-test.nsp";
    const auto bytes = make_nsp();
    expect(write_file(path, bytes), "write synthetic PFS0 NSP fixture");

    NspPackageInfo info{};
    std::string error;
    expect(inspect_nsp_file(path, info, &error),
           "inspect a valid NSP PFS0 directory");
    expect(info.file_count == 4U && info.files.size() == 4U,
           "read NSP entry count and directory entries");
    expect(info.nca_count == 2U && info.ticket_count == 1U &&
           info.certificate_count == 1U && info.metadata_nca_count == 1U,
           "classify NCA, CNMT NCA, ticket, and certificate entries");
    expect(info.contains_nca_content && !info.launchable_by_current_engine,
           "recognize protected NCA content without claiming it is launchable");
    expect(info.package_size == bytes.size() &&
           info.files[0].name.ends_with(".nca") &&
           info.files[0].size == 64U,
           "report package size, names, and per-file lengths");

    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
}

void test_reject_invalid_packages() {
    using namespace lucid::switchfmt;
    const auto path = std::filesystem::temp_directory_path() /
                      "lucidswitch2-package-invalid.nsp";
    auto bytes = make_nsp();
    NspPackageInfo info{};
    std::string error;

    bytes[0] = 'X';
    expect(write_file(path, bytes), "write bad-magic NSP fixture");
    expect(!inspect_nsp_file(path, info, &error),
           "reject a non-PFS0 package");

    bytes = make_nsp();
    write_u32(bytes, 4U, MaximumNspEntryCount + 1U);
    expect(write_file(path, bytes), "write excessive-entry NSP fixture");
    expect(!inspect_nsp_file(path, info, &error),
           "reject an excessive PFS0 entry count");

    bytes = make_nsp();
    write_u32(bytes, 12U, 1U);
    expect(write_file(path, bytes), "write reserved-header NSP fixture");
    expect(!inspect_nsp_file(path, info, &error),
           "reject a nonzero PFS0 reserved header word");

    bytes = make_nsp();
    write_u32(bytes, 0x10U + 16U, 0xFFFFFFFFU);
    expect(write_file(path, bytes), "write invalid-name-offset NSP fixture");
    expect(!inspect_nsp_file(path, info, &error),
           "reject a PFS0 filename offset outside its string table");

    bytes = make_nsp();
    write_u64(bytes, 0x10U, UINT64_MAX);
    expect(write_file(path, bytes), "write out-of-range data NSP fixture");
    expect(!inspect_nsp_file(path, info, &error),
           "reject a PFS0 data range outside the file");

    bytes = make_nsp();
    write_u64(bytes, 0x10U + 0x18U, 32U);
    write_u64(bytes, 0x10U + 0x18U + 8U, 64U);
    expect(write_file(path, bytes), "write overlapping data ranges NSP fixture");
    expect(!inspect_nsp_file(path, info, &error),
           "reject overlapping PFS0 file data ranges");

    const auto wrong_extension = path.parent_path() /
                                 "lucidswitch2-package-invalid.bin";
    expect(write_file(wrong_extension, make_nsp()),
           "write a PFS0 container with a non-NSP extension");
    expect(!inspect_nsp_file(wrong_extension, info, &error),
           "require an .nsp file extension for the NSP import command");

    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
    (void)std::filesystem::remove(wrong_extension, ignored);
}
} // namespace

int main() {
    test_valid_nsp_directory();
    test_reject_invalid_packages();
    if (failures != 0) {
        std::cerr << failures << " NSP package test(s) failed.\n";
        return 1;
    }
    std::cout << "All NSP package tests passed.\n";
    return 0;
}
