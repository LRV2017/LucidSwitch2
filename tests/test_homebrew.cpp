#include "lucid/homebrew/package.hpp"

#include <cstdint>
#include <filesystem>
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

lucid::homebrew::ProgramPackage make_program() {
    lucid::homebrew::ProgramPackage program;
    // MOVZ X0, #42; MOVZ X1, #0xC0DE; B . (original test instructions).
    program.code = {
        0x40U, 0x05U, 0x80U, 0xD2U,
        0xC1U, 0x1BU, 0x98U, 0xD2U,
        0x00U, 0x00U, 0x00U, 0x14U
    };
    program.data = {0x10U, 0x20U, 0x30U, 0x40U};
    program.bss_size = 128U;
    program.stack_size = 16U * 1024U;
    return program;
}

void write_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void test_round_trip() {
    using namespace lucid::homebrew;
    const ProgramPackage original = make_program();
    std::vector<std::uint8_t> encoded;
    std::string error;
    expect(encode_program(original, encoded, &error),
           "encode a valid LXP homebrew package");
    expect(encoded.size() == PackageHeaderSize + original.code.size() +
                             original.data.size(),
           "write a fixed header followed by code and data payloads");

    ProgramPackage decoded;
    expect(decode_program(encoded, decoded, &error),
           "decode a valid LXP homebrew package");
    expect(decoded.load_address == original.load_address &&
           decoded.entry_offset == original.entry_offset &&
           decoded.bss_size == original.bss_size &&
           decoded.stack_size == original.stack_size &&
           decoded.code == original.code && decoded.data == original.data,
           "preserve all package metadata and segment bytes");

    const auto path = std::filesystem::temp_directory_path() /
                      "lucidswitch2-homebrew-roundtrip.lxp";
    expect(write_program_file(path, original, &error),
           "write an LXP package to disk");
    ProgramPackage from_disk;
    expect(read_program_file(path, from_disk, &error),
           "read and validate an LXP package from disk");
    expect(from_disk.code == original.code && from_disk.data == original.data,
           "preserve code and data during file I/O");
    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
}

void test_rejection() {
    using namespace lucid::homebrew;
    std::string error;
    std::vector<std::uint8_t> valid;
    expect(encode_program(make_program(), valid, &error),
           "prepare a valid package for negative tests");
    if (valid.empty()) return;

    ProgramPackage output;
    auto damaged = valid;
    damaged[0] = static_cast<std::uint8_t>('X');
    expect(!decode_program(damaged, output, &error),
           "reject invalid package magic");

    damaged = valid;
    damaged[8] = 2U;
    expect(!decode_program(damaged, output, &error),
           "reject unsupported package versions");

    damaged = valid;
    write_u32(damaged, 12U, 1U);
    expect(!decode_program(damaged, output, &error),
           "reject unknown package flags");

    damaged = valid;
    damaged[PackageHeaderSize] ^= 0x01U;
    expect(!decode_program(damaged, output, &error),
           "reject payload corruption with CRC-32");

    damaged = valid;
    damaged.push_back(0U);
    expect(!decode_program(damaged, output, &error),
           "reject trailing bytes beyond declared payload lengths");

    damaged = valid;
    damaged[28] = 3U;
    damaged[29] = 0U;
    damaged[30] = 0U;
    damaged[31] = 0U;
    expect(!decode_program(damaged, output, &error),
           "reject truncated or inconsistent code segment sizes");

    ProgramPackage invalid = make_program();
    invalid.entry_offset = 4U;
    invalid.code.resize(4U);
    expect(!encode_program(invalid, valid, &error),
           "reject an entry point outside the code segment");

    invalid = make_program();
    invalid.load_address += 1U;
    expect(!encode_program(invalid, valid, &error),
           "reject an unaligned guest load address");

    invalid = make_program();
    invalid.stack_size = 4097U;
    expect(!encode_program(invalid, valid, &error),
           "reject a misaligned guest stack size");

    expect(!decode_program({}, output, &error),
           "reject an empty package without reading out of bounds");
}
}

int main() {
    test_round_trip();
    test_rejection();
    if (failures != 0) {
        std::cerr << failures << " homebrew test(s) failed.\n";
        return 1;
    }
    std::cout << "All homebrew package tests passed.\n";
    return 0;
}
