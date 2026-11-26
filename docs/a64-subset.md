# A64 Interpreter Subset

## Status

LucidSwitch2 contains an independently written, deliberately incomplete A64 interpreter for synthetic test programs. It is a CPU research component, not a complete ARMv8-A implementation and not evidence of Nintendo Switch 2 compatibility.

## Implemented instructions

- NOP
- MOVZ, MOVN and MOVK (32-bit and 64-bit forms)
- ADR and ADRP PC-relative address generation
- ADD/SUB (immediate) and ADDS/SUBS (immediate), including NZCV updates
- ADD/SUB (shifted register) and flag-setting variants
- AND, ORR, EOR, and ANDS (shifted-register and logical-immediate forms), including common MOV register alias encoding
- CSEL, CSINC, CSINV and CSNEG conditional-select instructions
- ADC/ADCS, SBC/SBCS, UDIV/SDIV, LSLV/LSRV/ASRV/RORV, and MADD/MSUB
- B, BL, B.cond, CBZ, and CBNZ
- PC-relative LDR W/X, LDRSW literal loads, plus PRFM literal as a no-op hint
- BR, BLR and RET
- BR and RET
- Unsigned-offset LDR and STR for 8-, 16-, 32-, and 64-bit values
- Unscaled, pre-index and post-index scalar integer LDR/STR, including signed byte/halfword/word loads for selected destination widths
- Register-offset integer LDR/STR with UXTW, LSL, SXTW and SXTX index extensions and optional element-size scaling
- Integer LDP/STP register pairs (W/X forms) with signed-offset, pre-index and post-index addressing, including LDPSW
- MRS and MSR for the selected system-register bank (CurrentEL, DAIF, SPSel, and selected EL1 registers)
- Scalar S/D floating-point arithmetic: FADD, FSUB, FMUL, and FDIV
- Scalar FMOV bit transfers between W/X general registers and S/D FP registers, plus S-to-S and D-to-D scalar register copies
- Initial Advanced SIMD integer vectors: ADD/SUB with 8/16/32/64-bit lanes in valid 64/128-bit arrangements, plus 64/128-bit AND/ORR/EOR

## Execution contract

Each call to A64Interpreter::step attempts exactly one instruction:

- Executed: instruction completed and PC advanced normally.
- Branch: control flow changed PC.
- UnsupportedInstruction: encoding is outside this subset; CPU state is left unchanged.
- MemoryFault: instruction fetch or data access failed, including a virtual translation or permission failure.
- SystemRegisterFault: an attempted write to a read-only modeled system register was rejected.
- MisalignedProgramCounter: PC was not 4-byte aligned.

CpuContext includes 31 general-purpose registers, 32 modeled 128-bit SIMD/FP registers, SP, PC, the retired-instruction counter, and NZCV flags. The SIMD/FP register file models 128 bits per V register. The implemented subset includes the listed scalar S/D operations and a small integer-vector group (ADD/SUB and AND/ORR/EOR); vector memory, lane insertion/extraction, saturation, comparisons, shifts, permutations, crypto, and vector floating-point remain unsupported. Register 31 is interpreted according to the instruction class: zero register for data-processing instructions, and SP for the supported load/store base and non-flag-setting ADD/SUB immediate source/destination cases.

The CPU-visible memory facade can translate every byte either through the standalone 4 KiB AddressSpace map or through Stage1Mmu. Emulator sessions use the stage-1 walker. Its deliberately limited regime is 4 KiB granules, lower 48-bit VAs, 48-bit PAs, TTBR0_EL1, and EL1 permission checks. Tests use synthetic page tables and hand-authored instruction words only; no console firmware or game files are involved. See mmu-stage1.md for its precise supported descriptor format.

## Not implemented

This is still a small subset. Scalar FP arithmetic uses host IEEE single/double operations and does not model FPCR/FPSR, architectural rounding control, FP exception flags/traps, denormal modes, or full NaN behavior. Only vector integer ADD/SUB and bitwise AND/ORR/EOR are implemented. Most Advanced SIMD/NEON operations remain unsupported, including vector memory, lane operations, comparisons, saturation, permutations, vector floating-point, and crypto. Pair loads/stores currently support only general-purpose registers, not SIMD/FP register pairs. The interpreter also lacks exception entry/return completeness, full EL/security state, system-instruction traps, ASIDs, TTBR1, additional granule sizes, full TLB maintenance, caches, atomics/exclusives, SIMD/FP vector memory operations, unprivileged access, exclusive/atomic operations, and the complete instruction set. The stage-1 walker is not a complete ARMv8-A MMU and the interpreter is not sufficient to boot an operating system or commercial console software. Only the listed modeled system registers are decoded; all other encodings remain unsupported.

## Test strategy

Every implemented instruction family should have focused synthetic tests for results, flags, PC behavior, register-31 semantics, and memory faults. Unsupported encodings should fail explicitly rather than silently succeeding.
