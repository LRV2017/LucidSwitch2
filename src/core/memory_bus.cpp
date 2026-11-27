#include "lucid/core/memory_bus.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <mutex>
#include <shared_mutex>
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
    std::unique_lock lock(mutex_);
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
        region.exclusive_versions.resize(
            ((size - 1U) / ExclusiveGranuleBytes) + 1U, 0U);
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

bool MemoryBus::protect_region(const std::uint64_t base,
    const std::size_t size, const MemoryPermission permissions,
    std::string* error) {
    if (permissions == MemoryPermission::None) {
        set_error(error, "A memory region must retain at least one permission.");
        return false;
    }
    std::unique_lock lock(mutex_);
    for (Region& region : regions_) {
        if (region.base == base && region.bytes.size() == size) {
            region.permissions = permissions;
            return true;
        }
    }
    set_error(error, "The exact memory region to protect was not found.");
    return false;
}

bool MemoryBus::unmap_region(const std::uint64_t base,
    const std::size_t size, std::string* error) {
    std::unique_lock lock(mutex_);
    const auto it = std::find_if(regions_.begin(), regions_.end(),
        [base, size](const Region& region) {
            return region.base == base && region.bytes.size() == size;
        });
    if (it == regions_.end()) {
        set_error(error, "The exact memory region to unmap was not found.");
        return false;
    }
    regions_.erase(it);
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
    std::shared_lock lock(mutex_);
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
    std::unique_lock lock(mutex_);
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
    const std::size_t first_granule = offset / ExclusiveGranuleBytes;
    const std::size_t last_granule =
        (offset + input.size() - 1U) / ExclusiveGranuleBytes;
    for (std::size_t index = first_granule; index <= last_granule; ++index) {
        ++region->exclusive_versions[index];
        if (region->exclusive_versions[index] == 0U)
            ++region->exclusive_versions[index];
    }
    return true;
}

bool MemoryBus::read_exclusive(const std::uint64_t address,
    const std::span<std::uint8_t> output, std::uint64_t& version,
    std::string* error) const {
    if ((output.size() != 4U && output.size() != 8U) ||
        (address % output.size()) != 0U) {
        set_error(error, "Exclusive loads require aligned 4-byte or 8-byte accesses.");
        return false;
    }
    const Region* region = find_region(address);
    std::shared_lock lock(mutex_);
    if (region == nullptr || !has_permission(region->permissions, MemoryPermission::Read)) {
        set_error(error, "The exclusive-load address is unmapped or unreadable.");
        return false;
    }
    const auto offset = static_cast<std::size_t>(address - region->base);
    if (offset > region->bytes.size() ||
        output.size() > region->bytes.size() - offset) {
        set_error(error, "The exclusive load crosses a mapped-region boundary.");
        return false;
    }
    std::copy_n(region->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                output.size(), output.begin());
    version = region->exclusive_versions[offset / ExclusiveGranuleBytes];
    return true;
}

bool MemoryBus::write_exclusive(const std::uint64_t address,
    const std::span<const std::uint8_t> input,
    const std::uint64_t expected_version, bool& stored, std::string* error) {
    stored = false;
    if ((input.size() != 4U && input.size() != 8U) ||
        (address % input.size()) != 0U) {
        set_error(error, "Exclusive stores require aligned 4-byte or 8-byte accesses.");
        return false;
    }
    Region* region = find_region(address);
    std::unique_lock lock(mutex_);
    if (region == nullptr || !has_permission(region->permissions, MemoryPermission::Write)) {
        set_error(error, "The exclusive-store address is unmapped or unwritable.");
        return false;
    }
    const auto offset = static_cast<std::size_t>(address - region->base);
    if (offset > region->bytes.size() ||
        input.size() > region->bytes.size() - offset) {
        set_error(error, "The exclusive store crosses a mapped-region boundary.");
        return false;
    }
    const std::size_t granule = offset / ExclusiveGranuleBytes;
    if (region->exclusive_versions[granule] != expected_version) return true;

    std::copy(input.begin(), input.end(),
              region->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    ++region->exclusive_versions[granule];
    if (region->exclusive_versions[granule] == 0U)
        ++region->exclusive_versions[granule];
    stored = true;
    return true;
}

bool MemoryBus::can_access(std::uint64_t address, std::size_t size,
                            MemoryPermission permission) const {
    if (size == 0U) return true;
    std::shared_lock lock(mutex_);
    const Region* region = find_region(address);
    if (region == nullptr || !has_permission(region->permissions, permission)) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(address - region->base);
    return offset <= region->bytes.size() &&
        size <= region->bytes.size() - offset;
}
bool MemoryBus::read8(std::uint64_t address, std::uint8_t& value,
                      std::string* error) const {
    return read(address, std::span<std::uint8_t>(&value, 1U), error);
}
bool MemoryBus::write8(std::uint64_t address, std::uint8_t value,
                       std::string* error) {
    return write(address, std::span<const std::uint8_t>(&value, 1U), error);
}
std::size_t MemoryBus::region_count() const {
    std::shared_lock lock(mutex_);
    return regions_.size();
}
} // namespace lucid::core
