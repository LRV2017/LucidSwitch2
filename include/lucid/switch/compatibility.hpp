#pragma once

#include <iosfwd>
#include <string_view>

namespace lucid::switchfmt {

enum class HardwareTarget {
    OriginalSwitch,
    Switch2
};

enum class ImplementationStatus {
    MetadataOnly,
    Partial,
    HostOnly,
    NotImplemented
};

struct HardwareProfile {
    HardwareTarget target{};
    std::string_view name{};
    std::string_view soc{};
    std::string_view cpu{};
    std::string_view gpu{};
    std::string_view memory{};
    std::string_view operating_system{};
    std::string_view executable_notes{};
    std::string_view confidence{};
};

struct CompatibilityFeature {
    std::string_view subsystem{};
    std::string_view feature{};
    ImplementationStatus status{};
    std::string_view detail{};
};

[[nodiscard]] const HardwareProfile& hardware_profile(HardwareTarget target) noexcept;
[[nodiscard]] std::string_view to_string(HardwareTarget target) noexcept;
[[nodiscard]] std::string_view to_string(ImplementationStatus status) noexcept;

// Emits a target-specific inventory of existing foundations and missing systems.
// This is a research report, not a prediction of commercial-game compatibility.
void write_compatibility_report(HardwareTarget target, std::ostream& output);

} // namespace lucid::switchfmt
