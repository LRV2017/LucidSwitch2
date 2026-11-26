# Lucid eXecutable Program (LXP) format

Phase 6 introduces LXP, an original and intentionally small container for running synthetic A64 test programs in LucidSwitch2. It is not a Nintendo executable format, NSO/NSP/XCI reader, operating-system loader, or compatibility layer.

## Version 1 file layout

All integers are unsigned little-endian. The file is exactly one 48-byte header followed by the declared code bytes and data bytes. There are no implicit tables, relocations, compression streams, or trailing bytes.

| Offset | Size | Field | Version 1 rule |
|---:|---:|---|---|
| 0 | 8 | Magic | ASCII LUCIDXP followed by NUL |
| 8 | 2 | Version | 1 |
| 10 | 2 | Header size | 48 |
| 12 | 4 | Flags | 0; all other values rejected |
| 16 | 8 | Load address | 4 KiB aligned, lower 48-bit address |
| 24 | 4 | Entry offset | 4-byte aligned offset within code |
| 28 | 4 | Code size | 1 byte to 8 MiB, multiple of 4 |
| 32 | 4 | Data size | 0 to 8 MiB |
| 36 | 4 | BSS size | Zero-initialized memory, up to 16 MiB |
| 40 | 4 | Stack size | 4 KiB to 16 MiB, multiple of 16 |
| 44 | 4 | Payload CRC-32 | IEEE CRC-32 of code bytes followed by data bytes |

The payload starts at byte 48. The code segment comes first, followed immediately by the initialized data segment. The BSS segment occupies zero-filled guest RAM after those bytes. The stack is reserved at the top of guest RAM. The combined code, data, BSS, and stack footprint is limited to 64 MiB.

CRC-32 detects accidental corruption; it is not a signature, authenticity check, or security boundary. The loader validates lengths, ranges, alignment, version, flags, and checksum before creating an emulator session.

## Running the included original sample

The CLI can create an original register demonstration package without an external assembler:

    .\build\Release\lucidswitch2.exe --create-homebrew-sample .\build\register_demo.lxp
    .\build\Release\lucidswitch2.exe --inspect-homebrew .\build\register_demo.lxp
    .\build\Release\lucidswitch2.exe --run-homebrew .\build\register_demo.lxp 3

The source equivalent is in [samples/register_demo.s](../samples/register_demo.s). It loads X0 = 42, loads X1 = 0xC0DE, then branches to itself. A three-instruction run budget makes the register results deterministic and keeps the CPU session bounded.

A program runner invocation has a default budget of 100,000 instructions and accepts a caller-supplied budget up to 10,000,000. Budget exhaustion pauses the emulator. An unsupported instruction, memory fault, or other CPU fault is reported as a failed run.

## Deliberate limits

- Only the documented instruction subset in [a64-subset.md](a64-subset.md) executes.
- No syscalls, console APIs, graphics API, dynamic linking, relocations, threads, filesystem access from guest code, or guest operating system are provided.
- The loader does not parse or decrypt proprietary console executables or firmware.
- Packages are caller-supplied test artifacts. No commercial game, firmware, key, SDK, or proprietary asset is included.
