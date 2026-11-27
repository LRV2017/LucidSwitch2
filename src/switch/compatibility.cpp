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

constexpr std::array<CompatibilityFeature, 18U> kCommonFeatures{{
    {"CPU", "AArch64 scalar integer interpreter", ImplementationStatus::Partial,
     "MOVN, ADR/ADRP, logical immediates, conditional selects, division, variable shifts, multiply-accumulate and linked branches are covered; the instruction set remains incomplete."},
    {"CPU", "Scalar S/D floating-point and FMOV", ImplementationStatus::Partial,
     "Scalar single/double arithmetic and bit-preserving FMOV are modeled; FPCR/FPSR, traps, and full precision control are not."},
    {"CPU", "Advanced SIMD/NEON vector execution", ImplementationStatus::Partial,
     "Integer-vector ADD/SUB and 64/128-bit AND/ORR/EOR are implemented for selected arrangements. S/D/Q SIMD/FP register-pair LDP/STP and unsigned-offset S/D/Q LDR/STR are also covered. Structure/lane operations, most vector arithmetic, vector FP, and crypto remain unsupported."},
    {"CPU", "Atomics, exclusives, and pointer authentication", ImplementationStatus::Partial,
     "Aligned 32/64-bit LDXR/LDAXR/STXR/STLXR and CLREX use a shared 16-byte-granule reservation-version model. FEAT_LSE LDADD/LDCLR/LDEOR/LDSET/LDSMAX/LDSMIN/LDUMAX/LDUMIN/SWP and CAS/CASA/CASL/CASAL are implemented for byte/halfword/word/doubleword operands. CASP/CASPA/CASPL/CASPAL atomically compare and swap pairs of 32-bit words or 64-bit doublewords. Byte/halfword and pair exclusives, pointer authentication, private CPU caches and hardware-faithful memory ordering remain unsupported."},
    {"System", "General Horizon process loader and runtime", ImplementationStatus::NotImplemented,
     "The restricted NRO test loader is not a process manager and does not support general MOD0 relocation, NSO loading, launch arguments, or Horizon process state."},
    {"Memory", "Stage-1 address translation", ImplementationStatus::Partial,
     "One constrained 4 KiB / 48-bit TTBR0 regime; not a complete target MMU."},
    {"System", "Exception entry and SVC handling", ImplementationStatus::Partial,
     "Selected SVC/IRQ paths exist; no complete privilege or system-call ABI."},
    {"System", "EL0 SVC dispatch subset", ImplementationStatus::Partial,
     "Implements SVC 0x10 current-processor query and SVC 0x1E deterministic virtual-tick query. Other calls still enter the guest vector path."},
    {"System", "Cooperative process/thread scheduler and handle table", ImplementationStatus::Partial,
     "A bounded host-side substrate supports process/thread lifecycle, priority scheduling, separate per-process checked memory maps and A64 execution through process-local cached interpreters, wait/wake, manual/auto-reset events, counting semaphores, recursive mutexes, and process-owned typed handles; it remains a cooperative model, not Horizon-compatible."},
    {"IPC", "HIPC/CMIF raw-only framing and command registry", ImplementationStatus::Partial,
     "Bounds-checked HIPC headers, aligned SFCI/SFCO messages, and a host command registry are supported; handles, descriptors, domains, and Horizon services are not."},
    {"System", "Horizon service implementations", ImplementationStatus::NotImplemented,
     "No compatible service manager behavior, named system services, filesystem, audio, input, or display service implementations."},
    {"Graphics", "Guest console GPU command processor and shaders", ImplementationStatus::NotImplemented,
     "The native Vulkan backend runs Lucid's own test pipeline, not console GPU commands or shaders."},
    {"Graphics", "Host Vulkan renderer", ImplementationStatus::HostOnly,
     "Native host rendering exists for Lucid test geometry; it is not guest GPU compatibility."},
    {"Security", "Protected retail-content decryption and title launch", ImplementationStatus::NotImplemented,
     "No protected-content decryption or retail-title launch path is implemented."},
    {"Audio", "Host-neutral stereo PCM mixer", ImplementationStatus::Partial,
     "A 16-channel, 48 kHz signed-16-bit stereo mixer supports bounded queues, Q15 gain, and saturated summation. An optional SDL2 adapter can open a desktop audio device and poll up to eight SDL game controllers; no Horizon device services or console DSP are implemented."},
    {"Input", "Host-neutral controller snapshot hub", ImplementationStatus::Partial,
     "Up to eight synchronized button/axis/trigger snapshots are supported, with optional SDL2 physical game-controller polling; motion, NFC, rumble and Horizon input services are not connected."},
    {"CPU", "AArch64 generation-aware basic-block cache", ImplementationStatus::Partial,
     "Caches short validated instruction blocks and invalidates them when the backing memory generation changes. Blocks still execute through the interpreter; no native machine-code JIT or optimizing register allocator is present."},
    {"Graphics", "GPU virtual address-space mappings", ImplementationStatus::Partial,
     "A generic 4 KiB page-aligned GPU virtual memory map translates guest GPU addresses onto shared physical memory with read/write/execute permissions. It is not the Switch 2 Tegra GPU MMU or a console page-table implementation."}
}};

constexpr CompatibilityFeature kOriginalExecutableFeature{
    "Executable", "NRO0/NSO0 plaintext metadata inspection",
    ImplementationStatus::MetadataOnly,
    "Reads and bounds-checks selected metadata; no decompression, relocation, dynamic linking, loading, or execution."
};

constexpr CompatibilityFeature kSwitch2CpuClusterFeature{
    "CPU", "Eight-core Switch 2 CPU-context cluster",
    ImplementationStatus::Partial,
    "Eight independent A64 architectural contexts share synchronized guest memory. Deterministic round-robin and parallel host-worker runners are available; this is not a Horizon scheduler, a full ARM memory model, or a complete Cortex-A78C implementation."
};

constexpr CompatibilityFeature kSwitch2ExecutableFeature{
    "Executable", "Switch 2 title/package loading",
    ImplementationStatus::Partial,
    "A strict loader accepts ordinary static ELF64 little-endian AArch64 ET_EXEC homebrew segments with W^X permissions and BSS zero-fill. It does not load Nintendo containers, NSO/NRO variants for Switch 2, dynamic relocations, imports, or retail titles."
};

constexpr CompatibilityFeature kNspPackageFeature{
    "Container", "NSP/PFS0 directory inspection",
    ImplementationStatus::MetadataOnly,
    "Validates the PFS0 directory and lists NCA/ticket/certificate filenames only. Does not parse/decrypt NCA content or launch titles."
};

constexpr CompatibilityFeature kRestrictedNroFeature{
    "Executable", "Restricted plaintext NRO homebrew execution",
    ImplementationStatus::Partial,
    "A bounded A64 subset can run a minimal NRO with a direct entry branch and symbol-free relative MOD0 RELA/RELR relocations. No symbol resolution, dynamic imports, Horizon ABI, system services, or game runtime."
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
        print_feature(kNspPackageFeature, output);
        print_feature(kRestrictedNroFeature, output);
    } else {
        print_feature(kSwitch2CpuClusterFeature, output);
        print_feature(kSwitch2ExecutableFeature, output);
    }

    output << "\nCurrent conclusion: no commercial Switch or Switch 2 game is currently "
              "loadable or claimed compatible by this project.\n"
           << "This report inventories research foundations; status is not inferred "
              "from the host GPU or file extension.\n";
}

} // namespace lucid::switchfmt
