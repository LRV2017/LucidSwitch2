# Phase 2 Architecture Foundation

This milestone adds independently testable building blocks. It is not a complete ARMv8-A implementation or console hardware model.

## Added

- Selected system-register bank with explicit read-only CurrentEL handling.
- Exception state capture and EL1 vector-slot calculation.
- Explicit 4 KiB virtual-page mappings with read/write/execute permission translation.
- Deterministic virtual counter/compare timer.
- 64-source interrupt pending/enable/acknowledge controller.
- MMIO device interface and checked, non-overlapping device bus.
- Browser frontend prototype with home/library/settings, metadata-only local file import, and Gamepad API detection.

## Not yet integrated

The A64 interpreter still accesses the generic MemoryBus directly. AddressSpace is a separately tested translation component, not an ARM hardware descriptor walker or TLB. ExceptionRouter models selected EL1 state and vector offsets; it does not implement exception-level transitions, ERET, banked state, or full exception routing. SystemRegisters are not yet connected to MRS/MSR decoding. Timer outputs are not yet wired to interrupt lines, and the interrupt controller is not yet wired into CPU exception entry. MMIO devices are not yet mapped into the CPU load/store path.

The frontend is a browser prototype, not a native desktop application. It stores selected file names and sizes in browser local storage; it does not retain file contents, parse/decrypt packages, or launch games.

## Remaining architecture integration

1. Implement an ARMv8 stage-1 descriptor walker, TLB, ASIDs, and architectural translation faults.
2. Route CPU instruction fetch and every load/store through translation.
3. Implement exception levels, exception return, MRS/MSR and system-instruction traps.
4. Connect virtual timer output to interrupt lines and route interrupts to CPU exception entry.
5. Define documented MMIO register maps and connect device bus to guest loads/stores.
6. Add deterministic architectural tests and reference vectors.
7. Choose a native GUI framework and connect its library model to a local-only, verified content loader.
