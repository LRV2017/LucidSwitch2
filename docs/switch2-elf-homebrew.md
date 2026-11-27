# Switch 2 Static ELF Homebrew Loader and Runner

LucidSwitch2 can load a deliberately narrow, standalone ELF profile to test its AArch64 memory and execution foundation.

## Accepted image profile

- ELF64, little-endian, ELF version 1.
- `ET_EXEC` for machine `EM_AARCH64` (183).
- Program header entries of the standard 56-byte ELF64 size; at most 128 program headers.
- `PT_LOAD` segments with bounded file and memory sizes, congruent power-of-two alignment up to 2 MiB, non-overlapping guest virtual ranges below 48 bits, and a 512 MiB aggregate memory budget.
- Segment permissions are translated from ELF flags. Writable+executable mappings are rejected. The loader temporarily maps writable memory to copy bytes, zero-fills BSS, and then applies final permissions.
- The entry point must be 4-byte aligned and lie inside an executable load segment.

Loading is staged and bounded. If a mapping or copy fails, mappings created during that load attempt are removed; pre-existing mappings are left untouched.

## Run it

```powershell
lucidswitch2 --run-switch2-elf .\homebrew.elf
lucidswitch2 --run-switch2-elf .\homebrew.elf 500000
```

The runner creates a bounded kernel-test process with its own address space, reserves a generic 1 MiB stack, maps the ELF segments into that process, creates its main thread, schedules it, and executes the requested instruction budget through the process-local `A64BlockCache`. The cache reuses raw instruction words and invalidates them when a region generation changes. Execution still uses the A64 interpreter. This validates the path from ELF image to process and thread context; it does not provide Horizon TLS, process capabilities, syscall ABI, service sessions, or console application startup.

## Not included

This is not a Switch 2 proprietary executable/container parser. It does not support ELF `ET_DYN`, dynamic relocation, symbol resolution, imported modules, Horizon ABI setup, TLS, process capabilities, named services, filesystem access, audio/input/display services, GPU initialization, or protected retail content. Reaching the instruction budget only proves that the bounded test image retired that many instructions; it does not establish console or game compatibility.
