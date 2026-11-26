#include "lucid/gpu/guest_memory.hpp"

#include <algorithm>
#include <limits>

namespace lucid::gpu {

GuestGpuMemory::GuestGpuMemory(const std::uint64_t guest_base,
                               const std::size_t size)
    : guest_base_(guest_base), storage_(size) {
    const auto size64 = static_cast<std::uint64_t>(size);
    valid_ = size != 0U &&
        size64 <= std::numeric_limits<std::uint64_t>::max() - guest_base;
}

bool GuestGpuMemory::valid() const noexcept { return valid_; }
std::uint64_t GuestGpuMemory::guest_base() const noexcept { return guest_base_; }
std::size_t GuestGpuMemory::size() const noexcept { return storage_.size(); }

bool GuestGpuMemory::offset_of(const std::uint64_t address,
                               const std::size_t length,
                               std::size_t& offset) const noexcept {
    if (!valid_ || address < guest_base_) return false;
    const auto difference = address - guest_base_;
    if (difference > static_cast<std::uint64_t>(storage_.size())) return false;
    const auto candidate = static_cast<std::size_t>(difference);
    if (length > storage_.size() - candidate) return false;
    offset = candidate;
    return true;
}

bool GuestGpuMemory::read(const std::uint64_t address,
                          const std::span<std::uint8_t> output) const noexcept {
    std::size_t offset = 0U;
    if (!offset_of(address, output.size(), offset)) return false;
    std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(offset),
                output.size(), output.begin());
    return true;
}

bool GuestGpuMemory::write(const std::uint64_t address,
                           const std::span<const std::uint8_t> input) noexcept {
    std::size_t offset = 0U;
    if (!offset_of(address, input.size(), offset)) return false;
    std::copy(input.begin(), input.end(),
              storage_.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

bool GuestGpuMemory::fill(const std::uint64_t address,
                          const std::size_t length,
                          const std::uint8_t value) noexcept {
    std::size_t offset = 0U;
    if (!offset_of(address, length, offset)) return false;
    std::fill_n(storage_.begin() + static_cast<std::ptrdiff_t>(offset),
                length, value);
    return true;
}

} // namespace lucid::gpu
