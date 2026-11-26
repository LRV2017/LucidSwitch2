# Stage-1 MMU: Supported Subset

## Scope

Stage1Mmu is a clean-room, test-driven implementation of a deliberately constrained AArch64 stage-1 translation regime. It is a real descriptor walk over physical memory, but it is not a complete ARMv8-A MMU.

## Active translation regime

- TTBR0_EL1 only; lower 48-bit virtual addresses.
- TCR_EL1.T0SZ = 16 (48-bit input address region).
- TCR_EL1.TG0 = 0b00 (4 KiB translation granule).
- TCR_EL1.IPS = 0b101 (48-bit output physical address size).
- Four lookup levels: L0, L1, L2, and L3.
- L1 1 GiB blocks, L2 2 MiB blocks, and L3 4 KiB pages.
- EL1 permission interpretation only.

The index fields are VA[47:39], VA[38:30], VA[29:21], and VA[20:12]. The final 12 bits are the page offset.

## Descriptor behavior modeled

The walker reads each 64-bit little-endian descriptor from MemoryBus physical memory. It distinguishes invalid descriptors, table descriptors, L1/L2 block descriptors, and L3 page descriptors. It checks the access flag (AF), privileged read-only AP[1], privileged execute-never PXN, and the table-level no-write / PXNTable restrictions. The backing MemoryBus separately enforces whether the translated physical byte is actually mapped and readable, writable, or executable.

The software TLB stores up to 256 successful 4 KiB translations and their modeled write/execute permissions. Block mappings are cached as individual 4 KiB entries. The replacement policy is round-robin. Call invalidate_va() or invalidate_all() after modifying page tables. Invalid/faulting translations are not cached.

## Emulator bootstrap

Emulator::initialize() allocates an L0 root and the required L1 table pages after the configured guest RAM, fills L1 block descriptors for every 1 GiB span touched by that range, sets TTBR0_EL1 and TCR_EL1 in the modeled system-register bank, and configures Stage1Mmu. Physical MemoryBus mappings remain the final bounds check, so the broader 1 GiB block descriptor does not create backing memory where none was allocated.

## Not implemented

This subset does not implement TTBR1, ASIDs or address-space identifiers, 16 KiB / 64 KiB granules, configurable starting levels, 52-bit PA / LPA2, stage-2 translation, full hierarchical permission semantics, EL0 permission behavior, memory attribute decoding through MAIR, shareability/cacheability, hardware access-flag management, dirty-bit management, hardware page-walk caches, architectural fault syndromes, TLBI instructions, or all architectural constraints/reserved-bit checks. MRS/MSR writes to TTBR0_EL1 and TCR_EL1 currently change the modeled register bank but do not reconfigure an already-active Stage1Mmu.

## Reference material

The descriptor walk follows the public AArch64 4 KiB translation structure described in Arm's [A-profile Architecture Reference Manual, address-translation examples](https://support.arm.com/documentation/ddi0487/maa/-Part-K-Appendixes/-Appendix-K8-Address-Translation-Examples/-K8-1-AArch64-Address-translation-examples/-K8-1-2-Full-translation-flows-for-VMSAv8-64-address-translation), and the Arm [AArch64 memory-management guide](https://documentation-service.arm.com/static/655de9bdee7b9d0d88eb7dbb?token=).
