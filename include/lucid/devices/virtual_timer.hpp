#pragma once

#include <cstdint>

namespace lucid::devices {

class VirtualTimer final {
public:
    void set_compare(std::uint64_t value) noexcept;
    void set_enabled(bool enabled) noexcept;
    void advance(std::uint64_t ticks) noexcept;

    [[nodiscard]] std::uint64_t counter() const noexcept;
    [[nodiscard]] std::uint64_t compare() const noexcept;
    [[nodiscard]] bool interrupt_pending() const noexcept;
    void acknowledge() noexcept;

private:
    std::uint64_t counter_{};
    std::uint64_t compare_{};
    bool enabled_{};
    bool pending_{};
    bool expired_{};
};

} // namespace lucid::devices
