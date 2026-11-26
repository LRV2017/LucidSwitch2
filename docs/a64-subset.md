# A64 Interpreter Subset

## Status

LucidSwitch2 contains an independently written, deliberately incomplete A64 interpreter for synthetic test programs. It is a CPU research component, not a complete ARMv8-A implementation and not evidence of Nintendo Switch 2 compatibility.

## Implemented instructions

- NOP
- MOVZ and MOVK (32-bit and 64-bit forms)
- ADD/SUB (immediate) and ADDS/SUBS (immediate), including NZCV updates
- ADD/SUB (shifted register) and flag-setting variants
- AND, ORR, EOR, and ANDS (shifted register), including common MOV register alias encoding
- B, B.cond, CBZ, and CBNZ
- BR and RET
- Unsigned-offset LDR and STR for 8-, 16-, 32-, and 64-bit values
- MRS and MSR for the selected system-register bank (CurrentEL, DAIF, SPSel, and selected EL1 registers)

## Execution contract

Each call to A64Interpreter::step attempts exactly one instruction:

- Executed: instruction completed and PC advanced normally.
- Branch: control flow changed PC.
- UnsupportedInstruction: encoding is outside this subset; CPU state is left unchanged.
- MemoryFault: instruction fetch or data access failed, including a virtual translation or permission failure.
- SystemRegisterFault: an attempted write to a read-only modeled system register was rejected.
- MisalignedProgramCounter: PC was not 4-byte aligned.

CpuContext includes 31 general-purpose registers, SP, PC, the retired-instruction counter, and NZCV flags. Register 31 is interpreted according to the instruction class: zero register for data-processing instructions, and SP for the supported load/store base and non-flag-setting ADD/SUB immediate source/destination cases.

The CPU-visible memory facade can optionally translate every byte through the explicit 4 KiB AddressSpace map before accessing the physical MemoryBus. Instruction fetch, data read, and data write use separate translation permissions, and a store preflights all target bytes before committing. Emulator sessions currently establish an identity-mapped guest range so the normal run loop exercises this path. This is a software page map, not an ARM descriptor walker or TLB. Tests use hand-authored instruction words and synthetic memory only; no console firmware or game files are involved.

## Not implemented

This is still a small subset. It does not implement exception entry/return, full EL/security state, system-instruction traps, an ARM stage-1 descriptor walker, TLB/ASID behavior, caches, atomics, SIMD/FP, vector crypto, most A64 addressing modes, signed loads, exclusive accesses, or the complete instruction set. Only the listed modeled system registers are decoded; all other encodings remain unsupported. The interpreter is not sufficient to boot an operating system or commercial console software.

## Test strategy

Every implemented instruction family should have focused synthetic tests for results, flags, PC behavior, register-31 semantics, and memory faults. Unsupported encodings should fail explicitly rather than silently succeeding.
