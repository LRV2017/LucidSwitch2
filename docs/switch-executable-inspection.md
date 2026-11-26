# Plaintext Switch executable inspection

Phase 8 adds a read-only inspector for two publicly documented original-Switch executable containers: NRO0 and NSO0. It is a diagnostic parser, not a game loader.

## CLI

    .\build\Release\lucidswitch2.exe --inspect-switch-executable .\program.nro
    .\build\Release\lucidswitch2.exe --inspect-switch-executable .\module.nso

The command reports file format, version and flags, declared image size, BSS size, module ID, text/rodata/data ranges, and applicable compression/hash metadata. For NRO it also prints the entry-stub instruction, MOD0 offset, additional-header offset, and whether an ASET asset section was detected.

## NRO0

The classic NRO layout starts with a 0x10-byte NRO-start structure, followed by the NRO0 header. The NRO-start structure is 0x10 bytes long. The Homebrew ABI treats the word at offset 0x00 as the entry branch (libnx names this field unused in its structure), offset 0x04 contains the MOD0 header pointer, and offsets 0x08-0x0F are padding. The signature NRO0 begins at offset 0x10.

NRO0 fields include:
- version at 0x14, total image size at 0x18, and flags at 0x1C;
- text/rodata/data memory offsets and sizes beginning at 0x20;
- BSS size at 0x38 and the newer additional-header offset at 0x3C;
- 32-byte module ID at 0x40.

The inspector reports the entry-stub instruction and MOD0 header pointer separately.

The optional ASET section follows the declared NRO image and contains three offset/size pairs for icon, NACP, and RomFS data. Its section ranges are checked relative to the ASET header.

The inspector accepts the classic plaintext, uncompressed file-offset layout. It rejects NRO compression flag variants because it does not implement that variant's layout/decompression.

## NSO0

The NSO header is 0x100 bytes. The inspector reads version/flags, the text/rodata/data file offsets, guest memory offsets and memory sizes, BSS size, 32-byte module ID, and stored compressed sizes when the corresponding flags are set.

It reports the per-segment LZ4-compression flags, hash-check flags, execute-only text flag, and Zstd flag. Compressed bytes are not decompressed and SHA-256 segment hashes are not verified.

## Validation and safety limits

- Input file size is limited to 256 MiB.
- Individual stored and memory segments are limited to 128 MiB.
- Combined segment memory is limited to 512 MiB.
- File and memory ranges are checked for overflow and overlap.
- NRO ASET section offsets/sizes are checked against the bytes present in the input file.
- Unknown flags, unsupported versions, nonzero reserved NSO header word, and unsupported NRO compression variants are rejected.
- The parser never executes supplied bytes.

## Compatibility boundary

NRO0 and NSO0 are original-Switch formats. Their recognition does not establish Switch 2 retail-game compatibility. Retail installed content commonly uses NCA containers rather than loose NRO/NSO files.

This component does not retrieve or derive production keys or title keys, process protected NCA content, decrypt, load, or execute retail game content. It is intended for read-only inspection of developer-supplied plaintext homebrew artifacts.

## References

- [NRO0 and NRO-start structure](https://switchbrew.org/wiki/NRO0)
- [NSO0 header and flags](https://switchbrew.org/wiki/NSO0)
- [NCA content archive](https://switchbrew.org/wiki/NCA)
