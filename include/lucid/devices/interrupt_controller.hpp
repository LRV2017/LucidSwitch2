#pragma once

#include <cstdint>

namespace lucid::devices {

class InterruptController final {
public:
    static constexpr unsigned InterruptCount = 64U;
    static constexpr unsigned TimerInterruptId = 30U;

    [[nodiscard]] bool set_enabled(unsigned id, bool enabled) noexcept;
    [[nodiscard]] bool raise(unsigned id) noexcept;
    [[nodiscard]] bool clear(unsigned id) noexcept;
    [[nodiscard]] bool pending(unsigned id) const noexcept;
    [[nodiscard]] bool acknowledge(unsigned& id) noexcept;
    [[nodiscard]] std::uint64_t pending_mask() const noexcept;

private:
    std::uint64_t enabled_{};
    std::uint64_t pending_{};
};

} // namespace lucid::devices
