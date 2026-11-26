# Exception and interrupt model

This milestone adds a deliberately small, testable exception path on top of the A64 interpreter.

## Implemented

- EL0 and EL1 are represented in CurrentEL; the architectural read value uses the EL << 2 encoding.
- SVC with a 16-bit immediate records the next PC in ELR_EL1, writes the SVC exception class and immediate to ESR_EL1, saves a modeled PSTATE in SPSR_EL1, and selects an EL1 vector slot through VBAR_EL1.
- Exception entry switches to EL1h and sets the modeled DAIF mask bits.
- ERET validates and restores EL0t, EL1t, or EL1h, the saved PC, DAIF, and NZCV.
- EL0 access to modeled privileged EL1 system registers is rejected. MRS CurrentEL remains available.
- The emulator advances a deterministic virtual counter by one tick per retired interpreter step. Timer compare expiration raises interrupt-controller source 30.
- An unmasked IRQ is acknowledged and routed to the correct EL1 vector slot. The timer is acknowledged when its interrupt is delivered.
- The timer is one-shot until a new compare value is programmed or it is disabled and re-enabled.

## Vector slots in this subset

The model supports AArch64 lower-EL entry and current-EL entry using SP_EL0 or SP_ELx. It calculates the vector address from VBAR_EL1 aligned down to 2 KiB, plus the architectural 128-byte slot offset for synchronous, IRQ, FIQ, or SError exceptions.

## Deliberate limitations

This is not a full ARM exception implementation. It does not yet model EL2/EL3, AArch32 lower-EL vectors, full PSTATE/SPSR semantics, exception return faults, debug exceptions, abort syndrome details, stack switching, a full GIC, interrupt priorities, active-state tracking, or hardware timer register interfaces. Data/instruction translation faults still surface as interpreter memory faults rather than architectural abort exceptions. MMIO routing is not yet connected to CPU load/store execution.

The timer interrupt is a deterministic emulator scheduling device, not a cycle-accurate implementation of the Switch 2 timer hardware.
