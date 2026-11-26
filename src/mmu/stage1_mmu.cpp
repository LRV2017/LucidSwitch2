#include "lucid/mmu/stage1_mmu.hpp"

#include <array>
#include <limits>

namespace lucid::mmu {
namespace {

constexpr std::uint64_t kDescriptorValid = 1ULL;
constexpr std::uint64_t kDescriptorTypeMask = 3ULL;
constexpr std::uint64_t kDescriptorTableOrPage = 3ULL;
constexpr std::uint64_t kDescriptorBlock = 1ULL;
constexpr std::uint64_t kAccessFlag = 1ULL << 10U;
constexpr std::uint64_t kApReadOnly = 1ULL << 7U;
constexpr std::uint64_t kPxn = 1ULL << 53U;
constexpr std::uint64_t kPxnTable = 1ULL << 59U;
constexpr std::uint64_t kApTableNoWrite = 1ULL << 61U;
constexpr std::uint64_t kPhysicalAddressMask = 0x0000FFFFFFFFFFFFULL;

[[nodiscard]] bool read_descriptor(const core::MemoryBus& memory,
                                   const std::uint64_t address,
                                   std::uint64_t& descriptor) noexcept {
    std::array<std::uint8_t, 8> bytes{};
    if (!memory.read(address, bytes)) return false;

    descriptor = 0U;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        descriptor |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return true;
}

[[nodiscard]] std::uint64_t level_shift(const unsigned level) noexcept {
    return 39U - (static_cast<std::uint64_t>(level) * 9U);
}

[[nodiscard]] std::uint64_t index_for_level(const std::uint64_t va,
                                            const unsigned level) noexcept {
    return (va >> level_shift(level)) & 0x1FFU;
}

} // namespace

Stage1Mmu::Stage1Mmu(core::MemoryBus& physical_memory) noexcept
    : physical_memory_(physical_memory) {}

bool Stage1Mmu::configure(const std::uint64_t ttbr0_el1,
                          const std::uint64_t tcr_el1) noexcept {
    constexpr std::uint64_t t0sz_mask = 0x3FULL;
    constexpr std::uint64_t epd0_mask = 1ULL << 7U;
    constexpr std::uint64_t tg0_mask = 3ULL << 14U;
    constexpr std::uint64_t ips_mask = 7ULL << 32U;

    const bool supported =
        (tcr_el1 & t0sz_mask) == TcrT0sz48Bit &&
        (tcr_el1 & epd0_mask) == 0U &&
        (tcr_el1 & tg0_mask) == 0U &&
        (tcr_el1 & ips_mask) == TcrIps48Bit;
    const std::uint64_t root = ttbr0_el1 & kPhysicalAddressMask & ~(PageSize - 1U);
    const bool ttbr_address_valid =
        (ttbr0_el1 & 0x0000FFFFFFFFF000ULL) == root &&
        (ttbr0_el1 & 0x0000000000000FFEU) == 0U &&
        (root & (PageSize - 1U)) == 0U;

    if (!supported || !ttbr_address_valid) {
        configured_ = false;
        invalidate_all();
        return false;
    }

    ttbr0_el1_ = root;
    tcr_el1_ = tcr_el1;
    configured_ = true;
    invalidate_all();
    return true;
}

bool Stage1Mmu::configured() const noexcept { return configured_; }

Translation Stage1Mmu::translate(const std::uint64_t virtual_address,
                                 const AccessType access) noexcept {
    if (!configured_) {
        return {TranslationFault::UnsupportedConfiguration, 0U};
    }
    if ((virtual_address >> 48U) != 0U) {
        return {TranslationFault::AddressOverflow, 0U};
    }

    const std::uint64_t virtual_page = virtual_address & ~(PageSize - 1U);
    for (TlbEntry& entry : tlb_) {
        if (!entry.valid || entry.virtual_page != virtual_page) continue;
        ++tlb_hits_;
        if (access == AccessType::Write && !entry.writable) {
            return {TranslationFault::PermissionDenied, 0U};
        }
        if (access == AccessType::Execute && !entry.executable) {
            return {TranslationFault::PermissionDenied, 0U};
        }
        return {TranslationFault::None,
                entry.physical_page | (virtual_address & (PageSize - 1U))};
    }

    ++tlb_misses_;
    const Translation result = walk(virtual_address, access);
    if (!result) return result;

    const Translation read_check = walk(virtual_address, AccessType::Read);
    const Translation write_check = walk(virtual_address, AccessType::Write);
    const Translation execute_check = walk(virtual_address, AccessType::Execute);
    if (!read_check) return read_check;

    TlbEntry& entry = tlb_[next_victim_];
    if (!entry.valid) ++tlb_entries_;
    entry.virtual_page = virtual_page;
    entry.physical_page = result.physical_address & ~(PageSize - 1U);
    entry.writable = static_cast<bool>(write_check);
    entry.executable = static_cast<bool>(execute_check);
    entry.valid = true;
    next_victim_ = (next_victim_ + 1U) % TlbCapacity;
    return result;
}

Translation Stage1Mmu::walk(const std::uint64_t virtual_address,
                            const AccessType access) const noexcept {
    std::uint64_t table_base =
        ttbr0_el1_ & kPhysicalAddressMask & ~(PageSize - 1U);
    bool inherited_read_only = false;
    bool inherited_pxn = false;

    for (unsigned level = 0U; level <= 3U; ++level) {
        const std::uint64_t descriptor_address =
            table_base + index_for_level(virtual_address, level) * 8U;
        std::uint64_t descriptor = 0U;
        if (!read_descriptor(physical_memory_, descriptor_address, descriptor)) {
            return {TranslationFault::TableWalkFault, 0U};
        }

        if ((descriptor & kDescriptorValid) == 0U) {
            return {TranslationFault::Unmapped, 0U};
        }

        const std::uint64_t type = descriptor & kDescriptorTypeMask;
        if (level == 0U ||
            (type == kDescriptorTableOrPage && level < 3U)) {
            if (type != kDescriptorTableOrPage || level == 3U ||
                (descriptor & 0x0000000000000FFCU) != 0U) {
                return {TranslationFault::InvalidDescriptor, 0U};
            }
            inherited_read_only =
                inherited_read_only || (descriptor & kApTableNoWrite) != 0U;
            inherited_pxn = inherited_pxn || (descriptor & kPxnTable) != 0U;
            table_base = descriptor & kPhysicalAddressMask & ~(PageSize - 1U);
            continue;
        }

        const bool is_block = type == kDescriptorBlock && (level == 1U || level == 2U);
        const bool is_page = type == kDescriptorTableOrPage && level == 3U;
        if (!is_block && !is_page) {
            return {TranslationFault::InvalidDescriptor, 0U};
        }
        if ((descriptor & kAccessFlag) == 0U) {
            return {TranslationFault::AccessFlagFault, 0U};
        }

        const bool read_only =
            inherited_read_only || (descriptor & kApReadOnly) != 0U;
        const bool executable =
            !inherited_pxn && (descriptor & kPxn) == 0U;
        if (access == AccessType::Write && read_only) {
            return {TranslationFault::PermissionDenied, 0U};
        }
        if (access == AccessType::Execute && !executable) {
            return {TranslationFault::PermissionDenied, 0U};
        }

        const std::uint64_t mapping_size =
            1ULL << (12U + (3U - level) * 9U);
        const std::uint64_t mapping_mask =
            kPhysicalAddressMask & ~(mapping_size - 1U);
        const std::uint64_t output_base = descriptor & mapping_mask;
        const std::uint64_t offset = virtual_address & (mapping_size - 1U);
        return {TranslationFault::None, output_base | offset};
    }

    return {TranslationFault::InvalidDescriptor, 0U};
}

void Stage1Mmu::invalidate_all() noexcept {
    for (TlbEntry& entry : tlb_) entry.valid = false;
    tlb_entries_ = 0U;
    next_victim_ = 0U;
}

void Stage1Mmu::invalidate_va(const std::uint64_t virtual_address) noexcept {
    const std::uint64_t page = virtual_address & ~(PageSize - 1U);
    for (TlbEntry& entry : tlb_) {
        if (entry.valid && entry.virtual_page == page) {
            entry.valid = false;
            --tlb_entries_;
            return;
        }
    }
}

std::size_t Stage1Mmu::tlb_entries() const noexcept { return tlb_entries_; }
std::uint64_t Stage1Mmu::tlb_hits() const noexcept { return tlb_hits_; }
std::uint64_t Stage1Mmu::tlb_misses() const noexcept { return tlb_misses_; }
std::uint64_t Stage1Mmu::ttbr0_el1() const noexcept { return ttbr0_el1_; }
std::uint64_t Stage1Mmu::tcr_el1() const noexcept { return tcr_el1_; }

} // namespace lucid::mmu
