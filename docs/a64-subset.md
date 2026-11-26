# A64 Interpreter Subset

## Status

This is a small, independently written interpreter for selected A64 instruction encodings. It is useful for validating the CPU execution interface with synthetic programs. It is **not** a complete ARMv8-A implementation and is not evidence of Nintendo Switch 2 compatibility.

## Implemented instructions

- `NOP`
- `MOVZ` (32-bit and 64-bit forms)
- `MOVK` (32-bit and 64-bit forms)
- `ADD (immediate)` and `SUB (immediate)`, without flag updates
- `B` (unconditional PC-relative branch)
- `CBZ` and `CBNZ`
- `BR` and `RET`

The subset currently treats register 31 as the zero register for supported data-processing operations. Stack-pointer semantics, condition flags, exceptions, privileged execution, SIMD/FP, atomics, system registers, and memory load/store instructions are not implemented.

## Execution contract

Each call to `A64Interpreter::step` attempts exactly one instruction:

- `Executed`: instruction completed and PC advanced normally.
- `Branch`: control flow changed PC.
- `UnsupportedInstruction`: encoding is outside this subset; CPU state is left unchanged.
- `MemoryFault`: instruction fetch failed.
- `MisalignedProgramCounter`: PC was not 4-byte aligned.

Instructions are fetched little-endian from the generic host-side `MemoryBus`. Tests use hand-authored instruction words and synthetic memory only; no console firmware or game files are involved.

## Next steps

1. Add arithmetic flag behavior and conditional branches.
2. Add register arithmetic and logical instructions.
3. Add scalar load/store operations and memory access faults.
4. Add a reusable A64 instruction test corpus based on public architectural specifications.
5. Expand coverage only when each new group has deterministic tests.
