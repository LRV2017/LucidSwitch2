#include "lucid/devices/controller_hub.hpp"

namespace lucid::devices {

bool ControllerHub::connect(const std::size_t slot) noexcept {
    if (slot >= MaxControllers) return false;
    std::lock_guard lock(mutex_);
    if (states_[slot].connected) return false;
    states_[slot] = {};
    states_[slot].connected = true;
    states_[slot].sequence = ++sequences_[slot];
    return true;
}

bool ControllerHub::disconnect(const std::size_t slot) noexcept {
    if (slot >= MaxControllers) return false;
    std::lock_guard lock(mutex_);
    if (!states_[slot].connected) return false;
    states_[slot] = {};
    states_[slot].sequence = ++sequences_[slot];
    return true;
}

bool ControllerHub::publish(const std::size_t slot,
                            const ControllerSnapshot& state) noexcept {
    if (slot >= MaxControllers ||
        (state.buttons & ~KnownButtonsMask) != 0U ||
        state.left_trigger > MaxTriggerValue ||
        state.right_trigger > MaxTriggerValue) return false;
    std::lock_guard lock(mutex_);
    if (!states_[slot].connected) return false;
    ControllerSnapshot next = state;
    next.connected = true;
    next.sequence = ++sequences_[slot];
    states_[slot] = next;
    return true;
}

bool ControllerHub::read(const std::size_t slot,
                         ControllerSnapshot& output) const noexcept {
    if (slot >= MaxControllers) return false;
    std::lock_guard lock(mutex_);
    if (!states_[slot].connected) return false;
    output = states_[slot];
    return true;
}

} // namespace lucid::devices
