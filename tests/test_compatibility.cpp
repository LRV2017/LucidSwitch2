#include "lucid/switch/compatibility.hpp"

#include <iostream>
#include <sstream>
#include <string>

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

void test_hardware_profiles() {
    using namespace lucid::switchfmt;
    const auto& original = hardware_profile(HardwareTarget::OriginalSwitch);
    const auto& next_generation = hardware_profile(HardwareTarget::Switch2);

    expect(original.target == HardwareTarget::OriginalSwitch &&
           original.name.find("original") != std::string_view::npos,
           "identify original Switch as a separate hardware target");
    expect(original.soc.find("Tegra X1") != std::string_view::npos,
           "record the community-identified Tegra X1 lineage");
    expect(next_generation.target == HardwareTarget::Switch2 &&
           next_generation.name.find("Switch 2") != std::string_view::npos,
           "identify Switch 2 as a separate hardware target");
    expect(next_generation.cpu.find("Cortex-A78C") != std::string_view::npos &&
           next_generation.gpu.find("Ampere") != std::string_view::npos,
           "record the separately researched Switch 2 CPU and GPU architecture");
    expect(next_generation.executable_notes.find("not proof") != std::string_view::npos,
           "avoid treating original Switch executable parsing as Switch 2 compatibility");
}

void test_compatibility_reports() {
    using namespace lucid::switchfmt;
    std::ostringstream original;
    write_compatibility_report(HardwareTarget::OriginalSwitch, original);
    const std::string original_text = original.str();
    expect(original_text.find("NRO0/NSO0 plaintext metadata inspection") != std::string::npos,
           "report original Switch NRO/NSO metadata-only support");
    expect(original_text.find("[partial] CPU / AArch64 scalar integer interpreter") != std::string::npos,
           "report expanded but incomplete AArch64 scalar execution");
    expect(original_text.find("[not implemented] System / General Horizon process loader and runtime") != std::string::npos,
           "report the missing general Horizon process runtime");
    expect(original_text.find("[not implemented] System / HIPC/CMIF") != std::string::npos,
           "report missing Horizon IPC and service implementations");
    expect(original_text.find("[partial] CPU / Scalar S/D floating-point and FMOV") != std::string::npos,
           "report the initial scalar floating-point subset as partial");
    expect(original_text.find("[not implemented] CPU / Advanced SIMD/NEON vector execution") != std::string::npos,
           "keep Advanced SIMD/NEON explicitly unsupported");
    expect(original_text.find("[metadata-only] Container / NSP/PFS0 directory inspection") != std::string::npos,
           "report NSP package directory inspection as metadata-only");
    expect(original_text.find("[partial] Executable / Restricted plaintext NRO homebrew execution") != std::string::npos,
           "report restricted NRO homebrew execution as partial");
    expect(original_text.find("no commercial Switch or Switch 2 game") != std::string::npos,
           "state that the project does not yet load commercial games");

    std::ostringstream switch2;
    write_compatibility_report(HardwareTarget::Switch2, switch2);
    const std::string switch2_text = switch2.str();
    expect(switch2_text.find("Switch 2") != std::string::npos,
           "produce a target-specific Switch 2 report");
    expect(switch2_text.find("Switch 2 title/package loading") != std::string::npos,
           "report Switch 2 title loading as not implemented");
    expect(switch2_text.find("NRO0/NSO0 plaintext metadata inspection") == std::string::npos,
           "do not advertise original Switch file parsing as Switch 2 support");
}

void test_status_labels() {
    using namespace lucid::switchfmt;
    expect(to_string(ImplementationStatus::MetadataOnly) == "metadata-only" &&
           to_string(ImplementationStatus::Partial) == "partial" &&
           to_string(ImplementationStatus::HostOnly) == "host-only" &&
           to_string(ImplementationStatus::NotImplemented) == "not implemented",
           "format compatibility status labels");
}
} // namespace

int main() {
    test_hardware_profiles();
    test_compatibility_reports();
    test_status_labels();
    if (failures != 0) {
        std::cerr << failures << " compatibility test(s) failed.\n";
        return 1;
    }
    std::cout << "All compatibility research tests passed.\n";
    return 0;
}
