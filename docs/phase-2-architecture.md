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

## CPU integration added in v0.5.0

The A64 interpreter now has a CPU-visible memory facade that translates instruction fetches and data loads/stores through `AddressSpace`, checks execute/read/write permissions, and preflights stores before committing them. The `Emulator` session maps its configured guest range identity-to-identity and executes through that translation path. Selected MRS/MSR encodings now read/write the existing system-register bank, with read-only CurrentEL writes rejected explicitly.

This remains a software page map, not an ARM stage-1 descriptor walker, TLB, ASID implementation, or full architectural fault model. Emulator sessions currently establish identity mappings; custom virtual/physical maps are exercised through the CPU memory interface in tests.

## Still not integrated

ExceptionRouter still only captures selected EL1 state and calculates vector offsets; it does not perform exception-level transitions, ERET, banked state, or complete exception routing. Timer outputs are not yet wired to interrupt lines, the interrupt controller is not wired into CPU exception entry, and MMIO devices are not mapped into the CPU load/store path. The frontend remains a browser prototype, not a native desktop application.

The frontend is a browser prototype, not a native desktop application. It stores selected file names and sizes in browser local storage; it does not retain file contents, parse/decrypt packages, or launch games.

## Remaining architecture integration

1. Implement an ARMv8 stage-1 descriptor walker, TLB, ASIDs, and architectural translation faults; replace the current identity-map bootstrap when architectural page tables are ready.
2. Expand MRS/MSR coverage and implement system-instruction traps with privilege checks.
3. Implement exception levels, vector entry, exception return, and banked state.
4. Connect virtual timer output to interrupt lines and route interrupts to CPU exception entry.
5. Define documented MMIO register maps and connect device bus to guest loads/stores.
6. Add deterministic architectural tests and reference vectors, ideally cross-checked against an independent reference implementation.
7. Choose a native GUI framework and connect its library model to a local-only, verified content loader.
