#include "lucid/switch/compatibility.hpp"

#include <array>
#include <ostream>

namespace lucid::switchfmt {
namespace {

constexpr HardwareProfile kOriginalSwitch{
    HardwareTarget::OriginalSwitch,
    "Nintendo Switch (original family)",
    "NVIDIA custom Tegra processor; community documentation identifies Tegra X1",
    "ARM Cortex-A57 family; AArch64",
    "NVIDIA Maxwell-generation GPU",
    "Shared system memory; exact usable allocation depends on system software",
    "Horizon OS",
    "NRO0 / NSO0 homebrew and main-module formats; installed retail content commonly uses NCA containers",
    "Nintendo confirms custom NVIDIA Tegra; finer SoC details are community-documented"
};

constexpr HardwareProfile kSwitch2{
    HardwareTarget::Switch2,
    "Nintendo Switch 2",
    "NVIDIA custom processor; community reverse engineering identifies T239",
    "Eight ARM Cortex-A78C cores; AArch64 with pointer-authentication extensions",
    "NVIDIA Ampere-generation GPU",
    "12 GB LPDDR5X reported by hardware reverse engineering",
    "Horizon OS generation for Switch 2; target-specific services and behavior require further research",
    "Switch 2 title/package path is not implemented; original NRO0/NSO0 inspection is not proof of Switch 2 support",
    "Nintendo confirms custom NVIDIA processor; T239-level detail is community reverse-engineered"
};

constexpr std::array<CompatibilityFeature, 10U> kCommonFeatures{{
    {"CPU", "AArch64 scalar interpreter", ImplementationStatus::Partial,
     "Only a small documented instruction subset is implemented; no general game compatibility."},
    {"CPU", "SIMD/NEON and floating-point execution", ImplementationStatus::NotImplemented,
     "Vector and FP architectural state and instruction coverage are missing."},
    {"CPU", "Atomics, exclusives, and pointer authentication", ImplementationStatus::NotImplemented,
     "Required synchronization and target-specific PAC semantics are not implemented."},
    {"Memory", "Stage-1 address translation", ImplementationStatus::Partial,
     "One constrained 4 KiB / 48-bit TTBR0 regime; not a complete target MMU."},
    {"System", "Exception entry and SVC handling", ImplementationStatus::Partial,
     "Selected SVC/IRQ paths exist; no complete privilege or system-call ABI."},
    {"System", "Process, thread, handle, and scheduler model", ImplementationStatus::NotImplemented,
     "No guest process runtime or scheduler."},
    {"System", "HIPC/CMIF service manager and Horizon services", ImplementationStatus::NotImplemented,
     "No IPC sessions, named services, filesystem, audio, input, or display service implementations."},
    {"Graphics", "Guest console GPU command processor and shaders", ImplementationStatus::NotImplemented,
     "The native Vulkan backend runs Lucid's own test pipeline, not console GPU commands or shaders."},
    {"Graphics", "Host Vulkan renderer", ImplementationStatus::HostOnly,
     "Native host rendering exists for Lucid test geometry; it is not guest GPU compatibility."},
    {"Security", "Protected retail-content decryption and title launch", ImplementationStatus::NotImplemented,
     "No protected-content decryption or retail-title launch path is implemented."}
}};

constexpr CompatibilityFeature kOriginalExecutableFeature{
    "Executable", "NRO0/NSO0 plaintext metadata inspection",
    ImplementationStatus::MetadataOnly,
    "Reads and bounds-checks selected metadata; no decompression, relocation, dynamic linking, loading, or execution."
};

constexpr CompatibilityFeature kSwitch2ExecutableFeature{
    "Executable", "Switch 2 title/package loading",
    ImplementationStatus::NotImplemented,
    "No Switch 2 retail package parser, authenticated loader, or application runtime."
};

constexpr CompatibilityFeature kRestrictedNroFeature{
    "Executable", "Restricted plaintext NRO homebrew execution",
    ImplementationStatus::Partial,
    "A bounded A64 subset can run a minimal NRO with a direct entry branch and no MOD0 metadata. No relocations, dynamic imports, Horizon ABI, system services, or game runtime."
};

void print_feature(const CompatibilityFeature& feature, std::ostream& output) {
    output << "  [" << to_string(feature.status) << "] "
           << feature.subsystem << " / " << feature.feature << "\n"
           << "      " << feature.detail << "\n";
}

} // namespace

const HardwareProfile& hardware_profile(const HardwareTarget target) noexcept {
    return target == HardwareTarget::OriginalSwitch ? kOriginalSwitch : kSwitch2;
}

std::string_view to_string(const HardwareTarget target) noexcept {
    switch (target) {
        case HardwareTarget::OriginalSwitch: return "switch";
        case HardwareTarget::Switch2: return "switch2";
    }
    return "unknown";
}

std::string_view to_string(const ImplementationStatus status) noexcept {
    switch (status) {
        case ImplementationStatus::MetadataOnly: return "metadata-only";
        case ImplementationStatus::Partial: return "partial";
        case ImplementationStatus::HostOnly: return "host-only";
        case ImplementationStatus::NotImplemented: return "not implemented";
    }
    return "unknown";
}

void write_compatibility_report(const HardwareTarget target, std::ostream& output) {
    const HardwareProfile& profile = hardware_profile(target);
    output << "LucidSwitch2 compatibility research report\n"
           << "Target: " << profile.name << "\n"
           << "SoC: " << profile.soc << "\n"
           << "CPU: " << profile.cpu << "\n"
           << "GPU: " << profile.gpu << "\n"
           << "Memory: " << profile.memory << "\n"
           << "OS model: " << profile.operating_system << "\n"
           << "Executable notes: " << profile.executable_notes << "\n"
           << "Hardware-source confidence: " << profile.confidence << "\n\n"
           << "Implementation inventory:\n";

    for (const auto& feature : kCommonFeatures) print_feature(feature, output);
    if (target == HardwareTarget::OriginalSwitch) {
        print_feature(kOriginalExecutableFeature, output);
        print_feature(kRestrictedNroFeature, output);
    } else {
        print_feature(kSwitch2ExecutableFeature, output);
    }

    output << "\nCurrent conclusion: no commercial Switch or Switch 2 game is currently "
              "loadable or claimed compatible by this project.\n"
           << "This report inventories research foundations; status is not inferred "
              "from the host GPU or file extension.\n";
}

} // namespace lucid::switchfmt
