#include "lucid/core/memory_bus.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
namespace lucid::core {
namespace {
void set_error(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}
[[nodiscard]] bool has_permission(MemoryPermission available,
                                  MemoryPermission requested) noexcept {
    const auto a = static_cast<std::uint8_t>(available);
    const auto r = static_cast<std::uint8_t>(requested);
    return (a & r) == r;
}
}
bool MemoryBus::map_region(std::uint64_t base, std::size_t size,
    std::string* error, MemoryPermission permissions) {
    if (size == 0U) {
        set_error(error, "A memory region cannot be empty.");
        return false;
    }
    if (permissions == MemoryPermission::None) {
        set_error(error, "A memory region must grant at least one permission.");
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
        region.permissions = permissions;
        region.bytes.resize(size, 0U);
        regions_.push_back(std::move(region));
    } catch (const std::bad_alloc&) {
        set_error(error, "Not enough host memory to allocate the requested region.");
        return false;
    } catch (const std::length_error&) {
        set_error(error, "The requested memory region is too large for this host.");
        return false;
    }
    return true;
}
MemoryBus::Region* MemoryBus::find_region(std::uint64_t address) noexcept {
    for (Region& region : regions_) {
        if (address >= region.base &&
            address - region.base < static_cast<std::uint64_t>(region.bytes.size()))
            return &region;
    }
    return nullptr;
}
const MemoryBus::Region* MemoryBus::find_region(std::uint64_t address) const noexcept {
    for (const Region& region : regions_) {
        if (address >= region.base &&
            address - region.base < static_cast<std::uint64_t>(region.bytes.size()))
            return &region;
    }
    return nullptr;
}
bool MemoryBus::read_with_permission(std::uint64_t address,
    std::span<std::uint8_t> output, MemoryPermission permission,
    std::string* error) const {
    if (output.empty()) return true;
    const Region* region = find_region(address);
    if (region == nullptr) {
        set_error(error, "The requested address is not mapped.");
        return false;
    }
    if (!has_permission(region->permissions, permission)) {
        set_error(error, permission == MemoryPermission::Execute
            ? "The requested region is not executable."
            : "The requested region is not readable.");
        return false;
    }
    const auto offset = static_cast<std::size_t>(address - region->base);
    if (offset > region->bytes.size() ||
        output.size() > region->bytes.size() - offset) {
        set_error(error, "The read crosses the end of a mapped region.");
        return false;
    }
    std::copy_n(region->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                output.size(), output.begin());
    return true;
}
bool MemoryBus::read(std::uint64_t address, std::span<std::uint8_t> output,
                     std::string* error) const {
    return read_with_permission(address, output, MemoryPermission::Read, error);
}
bool MemoryBus::read_instruction(std::uint64_t address,
    std::span<std::uint8_t> output, std::string* error) const {
    return read_with_permission(address, output, MemoryPermission::Execute, error);
}
bool MemoryBus::write(std::uint64_t address, std::span<const std::uint8_t> input,
                      std::string* error) {
    if (input.empty()) return true;
    Region* region = find_region(address);
    if (region == nullptr) {
        set_error(error, "The requested address is not mapped.");
        return false;
    }
    if (!has_permission(region->permissions, MemoryPermission::Write)) {
        set_error(error, "The requested region is not writable.");
        return false;
    }
    const auto offset = static_cast<std::size_t>(address - region->base);
    if (offset > region->bytes.size() ||
        input.size() > region->bytes.size() - offset) {
        set_error(error, "The write crosses the end of a mapped region.");
        return false;
    }
    std::copy(input.begin(), input.end(),
              region->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}
bool MemoryBus::read8(std::uint64_t address, std::uint8_t& value,
                      std::string* error) const {
    return read(address, std::span<std::uint8_t>(&value, 1U), error);
}
bool MemoryBus::write8(std::uint64_t address, std::uint8_t value,
                       std::string* error) {
    return write(address, std::span<const std::uint8_t>(&value, 1U), error);
}
std::size_t MemoryBus::region_count() const noexcept { return regions_.size(); }
} // namespace lucid::core
