#include "lucid/devices/virtual_timer.hpp"

#include <limits>

namespace lucid::devices {

void VirtualTimer::set_compare(const std::uint64_t value) noexcept {
    compare_ = value;
    expired_ = false;
    pending_ = false;
    if (enabled_ && counter_ >= compare_) {
        expired_ = true;
        pending_ = true;
    }
}

void VirtualTimer::set_enabled(const bool enabled) noexcept {
    enabled_ = enabled;
    if (!enabled_) {
        pending_ = false;
        expired_ = false;
        return;
    }
    if (!expired_ && counter_ >= compare_) {
        expired_ = true;
        pending_ = true;
    }
}

void VirtualTimer::advance(const std::uint64_t ticks) noexcept {
    counter_ = ticks > std::numeric_limits<std::uint64_t>::max() - counter_
        ? std::numeric_limits<std::uint64_t>::max()
        : counter_ + ticks;
    if (enabled_ && !expired_ && counter_ >= compare_) {
        expired_ = true;
        pending_ = true;
    }
}

std::uint64_t VirtualTimer::counter() const noexcept { return counter_; }
std::uint64_t VirtualTimer::compare() const noexcept { return compare_; }
bool VirtualTimer::interrupt_pending() const noexcept { return pending_; }

void VirtualTimer::acknowledge() noexcept {
    pending_ = false;
}

} // namespace lucid::devices
