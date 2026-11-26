#include "lucid/switch/homebrew_loader.hpp"

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

void write_magic(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                 const std::array<std::uint8_t, 4U> magic) {
    for (std::size_t index = 0U; index < magic.size(); ++index)
        bytes[offset + index] = magic[index];
}

std::vector<std::uint8_t> make_nro() {
    std::vector<std::uint8_t> bytes(0x88U, 0U);
    write_u32(bytes, 0x00U, 0x14000020U); // B +0x80: NRO entry stub.
    write_u32(bytes, 0x04U, 0U);         // No MOD0/dynamic image.
    write_magic(bytes, 0x10U, {'N', 'R', 'O', '0'});
    write_u32(bytes, 0x14U, 0U);
    write_u32(bytes, 0x18U, static_cast<std::uint32_t>(bytes.size()));
    write_u32(bytes, 0x1CU, 0U);
    write_u32(bytes, 0x20U, 0x80U);
    write_u32(bytes, 0x24U, 0x08U);
    write_u32(bytes, 0x28U, 0x88U);
    write_u32(bytes, 0x2CU, 0U);
    write_u32(bytes, 0x30U, 0x88U);
    write_u32(bytes, 0x34U, 0U);
    write_u32(bytes, 0x38U, 0x100U);

    // Minimal, original A64 payload: MOVZ X0,#42; B . (bounded run loop).
    bytes[0x80U] = 0x40U;
    bytes[0x81U] = 0x05U;
    bytes[0x82U] = 0x80U;
    bytes[0x83U] = 0xD2U;
    bytes[0x84U] = 0x00U;
    bytes[0x85U] = 0x00U;
    bytes[0x86U] = 0x00U;
    bytes[0x87U] = 0x14U;
    return bytes;
}


void write_u64(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index)
        bytes[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
}

std::vector<std::uint8_t> make_relocated_nro(const bool supported_relocation = true) {
    std::vector<std::uint8_t> bytes(0x1C0U, 0U);
    write_u32(bytes, 0x00U, 0x14000028U); // B +0xA0 to test instructions.
    write_u32(bytes, 0x04U, 0x120U);
    write_magic(bytes, 0x10U, {'N', 'R', 'O', '0'});
    write_u32(bytes, 0x14U, 0U);
    write_u32(bytes, 0x18U, static_cast<std::uint32_t>(bytes.size()));
    write_u32(bytes, 0x1CU, 0U);
    write_u32(bytes, 0x20U, 0x80U);
    write_u32(bytes, 0x24U, 0x100U);
    write_u32(bytes, 0x28U, 0x180U);
    write_u32(bytes, 0x2CU, 0x40U);
    write_u32(bytes, 0x30U, 0x1C0U);
    write_u32(bytes, 0x34U, 0U);
    write_u32(bytes, 0x38U, 0x100U);

    // MOVZ X1,#0x1C0; MOVK X1,#0x1000,LSL #16; LDR X0,[X1]; B .
    write_u32(bytes, 0xA0U, 0xD2803801U);
    write_u32(bytes, 0xA4U, 0xF2A20001U);
    write_u32(bytes, 0xA8U, 0xF9400020U);
    write_u32(bytes, 0xACU, 0x14000000U);

    // MOD0: .dynamic at 0x160; BSS range [0x1C0,0x2C0).
    write_magic(bytes, 0x120U, {'M', 'O', 'D', '0'});
    write_u32(bytes, 0x124U, 0x40U);
    write_u32(bytes, 0x128U, 0xA0U);
    write_u32(bytes, 0x12CU, 0x1A0U);

    write_u64(bytes, 0x160U, 7U);      // DT_RELA
    write_u64(bytes, 0x168U, 0x1A0U);
    write_u64(bytes, 0x170U, 8U);      // DT_RELASZ
    write_u64(bytes, 0x178U, 24U);
    write_u64(bytes, 0x180U, 0U);      // DT_NULL
    write_u64(bytes, 0x188U, 0U);

    write_u64(bytes, 0x1A0U, 0x1C0U);
    write_u64(bytes, 0x1A8U, supported_relocation ? 0x403U : 0x401U);
    write_u64(bytes, 0x1B0U, 0xA0U);
    return bytes;
}

bool write_fixture(const std::filesystem::path& path,
                   const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

void test_restricted_nro_execution() {
    using namespace lucid::switchfmt;
    const auto path = std::filesystem::temp_directory_path() /
                      "lucidswitch2-restricted-homebrew.nro";
    const auto bytes = make_nro();
    expect(write_fixture(path, bytes), "write a synthetic plaintext NRO fixture");

    NroHomebrewRunReport report{};
    std::string error;
    const bool loaded = run_restricted_nro_homebrew(path, 12U, report, &error);
    expect(loaded, "load and execute a bounded, relocation-free NRO test image");
    if (loaded) {
        expect(report.run_result == lucid::core::RunResult::InstructionLimitReached,
               "stop at the requested instruction budget");
        expect(report.instructions_retired == 12U,
               "retire exactly the requested number of A64 instructions");
        expect(report.register_x0 == 42U,
               "execute the NRO text segment and retain its register result");
        expect(report.entry_point == report.image_base + 0x80U,
               "resolve the Homebrew ABI entry branch into the text segment");
        expect((report.stack_pointer & 0xFU) == 0U,
               "initialize a 16-byte-aligned homebrew stack pointer");
    }

    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
}


void test_mod0_relative_relocation() {
    using namespace lucid::switchfmt;
    const auto path = std::filesystem::temp_directory_path() /
                      "lucidswitch2-mod0-relative.nro";
    const auto bytes = make_relocated_nro();
    expect(write_fixture(path, bytes),
           "write a plaintext NRO with MOD0 and a RELA relative relocation");
    NroHomebrewRunReport report{};
    std::string error;
    const bool loaded = run_restricted_nro_homebrew(path, 16U, report, &error);
    expect(loaded, "load a MOD0 NRO with a supported relative relocation");
    if (loaded) {
        expect(report.relocations_applied == 1U,
               "apply one validated R_AARCH64_RELATIVE relocation");
        expect(report.register_x0 == report.image_base + 0xA0U,
               "execute code that reads the relocated pointer from BSS");
    }

    auto damaged = make_relocated_nro(false);
    expect(write_fixture(path, damaged),
           "write NRO with an unsupported symbolic relocation");
    expect(!run_restricted_nro_homebrew(path, 8U, report, &error),
           "reject unsupported non-relative relocation types");

    damaged = make_relocated_nro();
    write_u64(damaged, 0x1A0U, 0xFFFFFFF0U);
    expect(write_fixture(path, damaged),
           "write NRO with an out-of-range RELA target");
    expect(!run_restricted_nro_homebrew(path, 8U, report, &error),
           "reject relocation targets outside the mapped image");
    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
}

void test_reject_unsupported_profiles() {
    using namespace lucid::switchfmt;
    std::error_code ignored;
    const auto path = std::filesystem::temp_directory_path() /
                      "lucidswitch2-restricted-homebrew-invalid.nro";
    auto bytes = make_nro();
    NroHomebrewRunReport report{};
    std::string error;

    expect(write_fixture(path, bytes), "write an NRO fixture for rejection tests");
    expect(!run_restricted_nro_homebrew(path, 0U, report, &error),
           "reject a zero instruction budget");
    expect(!run_restricted_nro_homebrew(path, MaximumNroInstructionBudget + 1U,
                                        report, &error),
           "reject an excessive instruction budget");

    write_u32(bytes, 0x00U, 0x94000020U); // BL, not B.
    expect(write_fixture(path, bytes), "write NRO with a linked branch stub");
    expect(!run_restricted_nro_homebrew(path, 8U, report, &error),
           "reject BL entry stubs outside the minimal loader ABI");

    bytes = make_nro();
    write_u32(bytes, 0x00U, 0x14000030U); // B +0xC0: beyond .text.
    expect(write_fixture(path, bytes), "write NRO with an out-of-text entry branch");
    expect(!run_restricted_nro_homebrew(path, 8U, report, &error),
           "reject entry branches that do not land inside .text");

    bytes = make_nro();
    write_u32(bytes, 0x1CU, 0x1U);
    expect(write_fixture(path, bytes), "write flagged NRO fixture");
    expect(!run_restricted_nro_homebrew(path, 8U, report, &error),
           "reject NRO flag variants outside the minimal runner profile");

    bytes = make_nro();
    write_u32(bytes, 0x1CU, 0x2U);
    expect(write_fixture(path, bytes), "write compressed-flag NRO fixture");
    expect(!run_restricted_nro_homebrew(path, 8U, report, &error),
           "reject compressed NRO variants in the restricted runner");

    bytes = make_nro();
    write_u32(bytes, 0x04U, 0x80U);
    expect(write_fixture(path, bytes), "write NRO with MOD0 metadata");
    expect(!run_restricted_nro_homebrew(path, 8U, report, &error),
           "reject a MOD0 pointer that does not reference a MOD0 header");

    (void)std::filesystem::remove(path, ignored);
}
} // namespace

int main() {
    test_restricted_nro_execution();
    test_mod0_relative_relocation();
    test_reject_unsupported_profiles();
    if (failures != 0) {
        std::cerr << failures << " restricted NRO runner test(s) failed.\n";
        return 1;
    }
    std::cout << "All restricted NRO runner tests passed.\n";
    return 0;
}
