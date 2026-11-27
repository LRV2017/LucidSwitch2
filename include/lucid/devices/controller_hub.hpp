#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace lucid::devices {

enum class ControllerButton : std::uint32_t {
    A = 1U << 0U, B = 1U << 1U, X = 1U << 2U, Y = 1U << 3U,
    L = 1U << 4U, R = 1U << 5U, ZL = 1U << 6U, ZR = 1U << 7U,
    Plus = 1U << 8U, Minus = 1U << 9U, LStick = 1U << 10U,
    RStick = 1U << 11U, Up = 1U << 12U, Down = 1U << 13U,
    Left = 1U << 14U, Right = 1U << 15U
};

[[nodiscard]] constexpr std::uint32_t button_bit(ControllerButton button) noexcept {
    return static_cast<std::uint32_t>(button);
}

struct ControllerSnapshot {
    bool connected{};
    std::uint32_t buttons{};
    std::int16_t left_x{};
    std::int16_t left_y{};
    std::int16_t right_x{};
    std::int16_t right_y{};
    std::uint16_t left_trigger{};
    std::uint16_t right_trigger{};
    std::uint64_t sequence{};
};

// Host-neutral controller state boundary. A platform adapter publishes
// normalized button/axis/trigger snapshots; device discovery, rumble, NFC,
// motion sensors, and Joy-Con rail protocols live in separate adapters.
class ControllerHub final {
public:
    static constexpr std::size_t MaxControllers = 8U;
    static constexpr std::uint16_t MaxTriggerValue = 32767U;
    static constexpr std::uint32_t KnownButtonsMask = 0x0000FFFFU;

    [[nodiscard]] bool connect(std::size_t slot) noexcept;
    [[nodiscard]] bool disconnect(std::size_t slot) noexcept;
    [[nodiscard]] bool publish(std::size_t slot, const ControllerSnapshot& state) noexcept;
    [[nodiscard]] bool read(std::size_t slot, ControllerSnapshot& output) const noexcept;

private:
    mutable std::mutex mutex_;
    std::array<ControllerSnapshot, MaxControllers> states_{};
    std::array<std::uint64_t, MaxControllers> sequences_{};
};

} // namespace lucid::devices
