# Phase 2 Architecture Foundation

This milestone adds independently testable building blocks. It is not a complete ARMv8-A implementation or console hardware model.

## Added

- Selected system-register bank with explicit read-only CurrentEL handling.
- Exception state capture and EL1 vector-slot calculation.
- Explicit software 4 KiB page mappings for isolated tests.
- Limited ARM stage-1 descriptor walker and fixed-size 256-entry software TLB.
- Deterministic virtual counter/compare timer.
- 64-source interrupt pending/enable/acknowledge controller.
- MMIO device interface and checked, non-overlapping device bus.
- Browser frontend prototype with home/library/settings, metadata-only local file import, and Gamepad API detection.

## CPU integration added in v0.6.0

The CPU-visible memory facade can now use an AArch64 stage-1 descriptor walker instead of only the standalone software AddressSpace map. It reads 64-bit descriptors from physical memory, follows L0-L3 tables for a 4 KiB granule and 48-bit lower virtual addresses, accepts L1 1 GiB and L2 2 MiB block descriptors plus L3 4 KiB pages, applies the modeled access flag / privileged read-only / privileged execute-never checks, and caches successful 4 KiB translations in a 256-entry software TLB. The emulator bootstrap builds an L0 table plus L1 identity-mapping blocks and executes CPU fetches and data accesses through this walker.

The active regime is intentionally narrow: TTBR0_EL1 only, T0SZ=16, TG0=4 KiB, IPS=48-bit, EL1 access checks, no ASIDs, no TTBR1, no 16/64 KiB granules, no hardware page-table walk cache, and no full architectural fault syndrome or TLB maintenance instruction implementation. Table edits require explicit invalidate_va or invalidate_all calls. The standalone AddressSpace remains available for small unit tests.

## Still not integrated

ExceptionRouter still only captures selected EL1 state and calculates vector offsets; it does not perform exception-level transitions, ERET, banked state, or complete exception routing. Timer outputs are not yet wired to interrupt lines, the interrupt controller is not wired into CPU exception entry, and MMIO devices are not mapped into the CPU load/store path. MRS/MSR writes to TTBR0_EL1 and TCR_EL1 update the modeled register bank but do not yet reconfigure the live Stage1Mmu. The frontend remains a browser prototype, not a native desktop application.

The frontend is a browser prototype, not a native desktop application. It stores selected file names and sizes in browser local storage; it does not retain file contents, parse/decrypt packages, or launch games.

## Remaining architecture integration

1. Complete the stage-1 translation regime: ASIDs, TTBR switching, TTBR1, configurable starting levels, full APTable/XN behavior, architectural fault records, and TLBI instructions.
2. Connect MRS/MSR writes to translation-context changes and implement privilege-aware system-instruction traps.
3. Implement exception levels, vector entry, exception return, and banked state.
4. Connect virtual timer output to interrupt lines and route interrupts to CPU exception entry.
5. Define documented MMIO register maps and connect device bus to guest loads/stores.
6. Add deterministic architectural tests and reference vectors, ideally cross-checked against an independent reference implementation.
7. Choose a native GUI framework and connect its library model to a local-only, verified content loader.
