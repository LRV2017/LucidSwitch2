# Switch and Switch 2 architecture research

## Scope and target separation

LucidSwitch2 now treats the original Nintendo Switch and Nintendo Switch 2 as separate compatibility targets. Shared AArch64 concepts do not imply identical CPU extensions, executable packaging, system services, GPU command streams, memory behavior, or security features.

The official Nintendo specification describes Switch 2 as using a custom NVIDIA processor. Community reverse-engineering documentation identifies the processor as NVIDIA T239 and reports eight Cortex-A78C CPU cores, an Ampere GPU, and 12 GB LPDDR5X memory. The original Switch is specified by Nintendo as using a custom Tegra processor; SwitchBrew identifies the family as Tegra X1. Treat detailed SoC configuration as reverse-engineering data rather than an official Nintendo compatibility specification.

## Target architecture profiles

| Area | Original Switch | Switch 2 |
|---|---|---|
| Official public CPU/GPU description | Custom NVIDIA Tegra processor | Custom NVIDIA processor |
| Community SoC identification | Tegra X1 family | NVIDIA T239 |
| CPU | Cortex-A57 family, AArch64 | 8 × Cortex-A78C, AArch64; pointer authentication support reported |
| GPU | NVIDIA Maxwell generation | NVIDIA Ampere generation |
| Memory | Shared system memory; software allocation depends on OS/runtime | 12 GB LPDDR5X reported by reverse engineering |
| Operating system | Horizon OS | Switch 2-specific Horizon OS environment; exact service behavior requires further research |
| Known executable formats | NRO0 homebrew, NSO0 main modules; installed content is commonly NCA-based | Target-specific application/content launch path is not implemented here |

Switch 2 hardware research reports 4 MB shared L3 cache, 256 KiB L2 per core, a 128-bit LPDDR5X-7500-class interface and an Ampere GPU with 1536 cores. It also reports ARM pointer-authentication features. Those details are attributed to the Switch 2 reverse-engineering community, not to Nintendo's public technical specification.

## Executable container model

### NRO0

NRO is used for non-ExeFS executable binaries and starts with a 0x10-byte NRO-start structure followed by the NRO0 header at offset 0x10. The start structure is 0x10 bytes: the Homebrew ABI uses the word at 0x00 as the entry branch, the MOD0 pointer is at 0x04, and 0x08-0x0F is padding. libnx names the first struct field unused, but the Homebrew ABI specifies that the loader enters at binary_ptr + 0 and the instruction branches past the NRO0 header. The NRO0 header contains the version, total file size, flags, text/rodata/data memory offsets and sizes, BSS size, an additional-header offset in newer versions, and a 32-byte module ID. NRO flags include an aligned-header bit and a compression bit introduced in later system software. Homebrew packages can append an optional ASET section containing icon, NACP, and RomFS ranges.

The current parser accepts the classic plaintext, uncompressed file-offset layout. It reports the entry branch and MOD0 pointer separately, and validates the start/header signature, version, flags, segment sizes/ranges, BSS bounds, MOD0 pointer, additional-header offset, and optional ASET section bounds. It explicitly rejects the compressed NRO variant rather than assuming classic offsets remain valid. Phase 9 adds a separate experimental runner for a very small profile: the NRO-start instruction must be a direct B-immediate branch into .text, and the MOD0 pointer must be zero.

### NSO0

NSO is the main executable module format. Its 0x100-byte header carries version and flags; text/rodata/data file offsets, guest memory offsets, and uncompressed sizes; BSS size; a 32-byte module ID; compressed segment sizes; and version-dependent metadata. Segment flags can indicate LZ4 compression, hash checks, execute-only text, and Zstd compression in newer formats.

The current parser accepts plaintext NSO0 headers and validates file and memory ranges, overlap, sizes, supported flags, and reserved fields. It reports compression and hash metadata, but does not decompress LZ4/Zstd data or verify segment hashes.

### NCA and installed game content

NCA is a content archive used by installed software. It is not a loose NRO/NSO executable. Recognizing NRO0 or NSO0 does not mean an emulator can parse an installed game package or launch a retail title. The Phase 8 code deliberately does not implement encrypted content loading.

## Horizon OS is part of the runtime

A game executable expects an operating-system environment, not just memory containing instructions. Major missing subsystems include:

- process creation, address spaces, page permissions, ASLR, and memory resource limits;
- AArch64 user/kernel exception transitions and a complete SVC system-call ABI;
- threads, scheduler, thread-local storage, synchronization, timers, handles, and waitables;
- HIPC message framing, CMIF domain/session handling, service-manager registration, and named services;
- filesystem and content access, input/controller services, audio, display, application lifecycle, and other system modules;
- target GPU command processing, shader execution, memory layouts, and compositor/display integration.

An application importing services that do not exist in the emulator will fail even if its file header is recognized and its CPU instructions decode.

## Guest GPU versus host GPU

The Phase 7 Vulkan backend runs LucidSwitch2's own vertex and fragment shaders with host Vulkan resources. It does not emulate Maxwell or Ampere guest GPU commands, translate proprietary shader programs, reproduce guest memory tiling/compression, or implement the console display compositor. Host Vulkan success is not evidence of guest GPU compatibility.

The original Switch and Switch 2 GPU paths must be researched separately. Their host renderer may share an output abstraction, but guest command processors and shader translation cannot be assumed interchangeable.

## Phase 8 implementation

Phase 8 adds:
- separate metadata profiles for original Switch and Switch 2;
- a bounded read-only parser for plaintext classic NRO0 and NSO0 metadata;
- segment/file/memory range validation, overlap checks, and size limits;
- optional NRO ASET section recognition and bounds checks;
- NSO compression, hash-check, and execute-only flag reporting;
- a CLI compatibility inventory that explicitly labels implemented, partial, metadata-only, host-only, and missing features.

It does not decrypt, decompress, relocate, dynamically link, load, or execute Switch/Switch 2 retail content or Horizon applications.

## Phase 9 restricted NRO execution prototype

Phase 9 adds the `--run-nro` command for a deliberately minimal original-Switch homebrew profile:

- accepts only plaintext, uncompressed NRO0 with a zero flags field;
- requires the NRO-start instruction at `binary_ptr + 0` to be a direct A64 B-immediate into the validated .text segment;
- rejects NRO files with a nonzero MOD0 pointer because dynamic metadata and relocations are not implemented;
- maps a bounded guest image, zero-filled BSS, and a 64 KiB stack;
- initializes the two documented NRO Homebrew ABI argument registers conservatively (X0 = null loader-config pointer, X1 = all ones);
- runs only the interpreter instruction subset and stops at a user-specified instruction budget (maximum 10,000,000).

This is an execution proof for synthetic/minimal NRO test images, not a complete Homebrew ABI environment. A real NRO that expects libnx runtime initialization, dynamic relocations, imported modules, SVCs, handles, threads, filesystem/input/audio/display services, or GPU access will stop or fault. The runner is not a general-purpose game launcher.

## Next implementation milestones

1. Expand the restricted NRO runner into a real homebrew loader: segment-specific guest page permissions, MOD0 parsing, dynamic relocations, and runtime initialization.
2. Expand AArch64 execution: SIMD/NEON, FP state, atomics/exclusives, architectural exceptions, and target-specific extensions such as pointer authentication where required.
3. Implement memory-management behavior, complete SVC transitions, process/thread state, scheduling, TLS, handles, and synchronization primitives.
4. Implement HIPC/CMIF message handling, session/domain behavior, a service manager, and a small documented homebrew service set.
5. Add filesystem, input, audio, display, and application lifecycle service models.
6. Research and implement separate original-Switch and Switch 2 guest GPU command/shader paths, backed by public or independently obtained test vectors.
7. Build compatibility test suites using original homebrew and independently created software; publish per-title results only when reproducible.
8. Only then assess broader user-provided software compatibility. A full retail-title path remains contingent on many additional technical, legal, and security boundaries.

These are engineering milestones, not a guarantee that commercial games will become compatible.

## Research references

See [research-sources.md](research-sources.md) for the supplied ROMSLAB workflow context and the primary NRO/Homebrew ABI references used by the project.
