#include "lucid/core/memory_bus.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace lucid::core {
namespace {
void set_error(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}
} // namespace

bool MemoryBus::map_region(const std::uint64_t base, const std::size_t size,
                           std::string* error) {
    if (size == 0U) {
        set_error(error, "A memory region cannot be empty.");
        return false;
    }

    const auto size64 = static_cast<std::uint64_t>(size);
    if (size64 > std::numeric_limits<std::uint64_t>::max() - base) {
        set_error(error, "The memory region address range overflows.");
        return false;
    }

    const std::uint64_t end = base + size64;
    for (const Region& region : regions_) {
        const auto existing_size = static_cast<std::uint64_t>(region.bytes.size());
        const std::uint64_t existing_end = region.base + existing_size;
        if (base < existing_end && region.base < end) {
            set_error(error, "The memory region overlaps an existing mapping.");
            return false;
        }
    }

    try {
        Region region;
        region.base = base;
        region.bytes.resize(size, 0U);
        regions_.push_back(std::move(region));
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to allocate the requested region.");
        return false;
    }

    return true;
}

MemoryBus::Region* MemoryBus::find_region(const std::uint64_t address) noexcept {
    for (Region& region : regions_) {
        if (address >= region.base &&
            address - region.base < static_cast<std::uint64_t>(region.bytes.size())) {
            return &region;
        }
    }
    return nullptr;
}

const MemoryBus::Region* MemoryBus::find_region(
    const std::uint64_t address) const noexcept {
    for (const Region& region : regions_) {
        if (address >= region.base &&
            address - region.base < static_cast<std::uint64_t>(region.bytes.size())) {
            return &region;
        }
    }
    return nullptr;
}

bool MemoryBus::read(const std::uint64_t address,
                     const std::span<std::uint8_t> output,
                     std::string* error) const {
    if (output.empty()) {
        return true;
    }

    const Region* region = find_region(address);
    if (region == nullptr) {
        set_error(error, "The requested address is not mapped.");
        return false;
    }

    const std::uint64_t offset64 = address - region->base;
    const auto offset = static_cast<std::size_t>(offset64);
    if (offset > region->bytes.size() ||
        output.size() > region->bytes.size() - offset) {
        set_error(error, "The read crosses the end of a mapped region.");
        return false;
    }

    std::copy_n(region->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                output.size(), output.begin());
    return true;
}

bool MemoryBus::write(const std::uint64_t address,
                      const std::span<const std::uint8_t> input,
                      std::string* error) {
    if (input.empty()) {
        return true;
    }

    Region* region = find_region(address);
    if (region == nullptr) {
        set_error(error, "The requested address is not mapped.");
        return false;
    }

    const std::uint64_t offset64 = address - region->base;
    const auto offset = static_cast<std::size_t>(offset64);
    if (offset > region->bytes.size() ||
        input.size() > region->bytes.size() - offset) {
        set_error(error, "The write crosses the end of a mapped region.");
        return false;
    }

    std::copy(input.begin(), input.end(),
              region->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

bool MemoryBus::read8(const std::uint64_t address, std::uint8_t& value,
                      std::string* error) const {
    return read(address, std::span<std::uint8_t>(&value, 1U), error);
}

bool MemoryBus::write8(const std::uint64_t address, const std::uint8_t value,
                       std::string* error) {
    return write(address, std::span<const std::uint8_t>(&value, 1U), error);
}

std::size_t MemoryBus::region_count() const noexcept {
    return regions_.size();
}

} // namespace lucid::core
