# A64 Interpreter Subset

## Status

This is a small, independently written interpreter for selected A64 instruction encodings. It is useful for validating the CPU execution interface with synthetic programs. It is **not** a complete ARMv8-A implementation and is not evidence of Nintendo Switch 2 compatibility.

## Implemented instructions

- `NOP`
- `MOVZ` (32-bit and 64-bit forms)
- `MOVK` (32-bit and 64-bit forms)
- `ADD/SUB (immediate)` and `ADDS/SUBS (immediate)`, including NZCV updates
- `B` (unconditional PC-relative branch)
- `B.cond` for the standard A64 condition codes
- Unsigned-offset `LDR` and `STR` for 8-, 16-, 32-, and 64-bit values
- `CBZ` and `CBNZ`
- `BR` and `RET`

## Execution contract

Each call to `A64Interpreter::step` attempts exactly one instruction:

- `Executed`: instruction completed and PC advanced normally.
- `Branch`: control flow changed PC.
- `UnsupportedInstruction`: encoding is outside this subset; CPU state is left unchanged.
- `MemoryFault`: instruction fetch failed.
- `MisalignedProgramCounter`: PC was not 4-byte aligned.

The interpreter stores the four NZCV flags in `CpuContext::flags`. The flag-setting immediate arithmetic instructions update them; ordinary ADD/SUB preserve the prior flags. Register 31 is treated as the zero register for the currently supported data-processing operations.

Instructions are fetched little-endian from the generic host-side `MemoryBus`. Tests use hand-authored instruction words and synthetic memory only; no console firmware or game files are involved.

## Not yet implemented

Pre/post-indexed and register-offset memory addressing, signed loads, register arithmetic, logical operations, exceptions, privileged execution, SIMD/FP, atomics, and system registers remain unsupported.

## Next steps

1. Add common register arithmetic and logical operations.
2. Add pre/post-indexed and register-offset memory addressing.
3. Expand tests for overflow, carry, signed conditions, and instruction edge cases.
4. Add a reusable A64 instruction test corpus based on public architectural specifications.
5. Expand coverage only when each new group has deterministic tests.
